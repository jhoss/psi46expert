#include "DataFilter.h"
#include <iostream>
#include <cstdio>
#include <cstring>
#include "BasePixel/RawPacketDecoder.h"

using namespace std;

#ifdef _WIN32
#define QWFMT "%15I64u"  // !!! only for windows
#else
#define QWFMT "%15llu"   // !!! probably works for Linux
#endif

// === CRawEvent ============================================================

void CRawEvent::PrintEventFlags()
{
	char c[] = "[VIrCRTtD]";
	unsigned int i, mask;


	for (i=1, mask=0x80; i<=8; i++, mask>>=1)
		if (!(flags & mask)) c[i] = '.';
	cout << c;
}


void CRawEvent::PrintDataFlags(unsigned int index)
{
	switch (dflag[index])
	{
		case 0: cout << " "; break;
		case 4: cout << "H"; break;
		case 1: cout << "T"; break;
		case 2: cout << "R"; break;
		case 3: cout << "E"; break;
		default: cout << "?";
	}
}


void CRawEvent::PrintData()
{
	unsigned int i, nPix;
	if (length<64) nPix = 0; else nPix = (length-64)/6;
	printf("<%4u/%3u>", length, nPix);
	for (i=0; i<length; i++)
	{
		printf("%6i", data[i]);
		PrintDataFlags(i);
	}
	cout << "\n";
}


void CRawEvent::Print()
{
	printf("%02X",flags>>8);
	PrintEventFlags();

	printf("(" QWFMT ")", time);

	PrintData();
}

// === CEvent ===============================================================

void CEvent::print()
{
	cout << (isReset ? 'R' : '.');
	cout << (isCalibrate ? 'C' : '.');
	cout << (isTrigger ? 'T' : '.');
	cout << (isData ? 'D' : '.');
	cout << (isOverflow ? 'V' : '.');
	cout << " " << timestamp;
	cout << endl;
	
	if (nHits > 0) {
		for (int r = 0; r < nRocs; r++) {
			int h = hits.roc[r].numPixelHits;
			for (int i = 0; i < h; i++) {
				cout << "ROC " << r << " hit: "
				<< hits.roc[r].pixelHit[i].columnROC
				<< " "
				<< hits.roc[r].pixelHit[i].rowROC << endl;
			}
		}
	}
}

/* Pipe which reads short integers from the testboard RAM. Does not read from any previous pipe. -------------- */

#define BLOCKSIZE 32768 /* Must be an even number! */

RAMRawDataReader::RAMRawDataReader(CTestboard * b, unsigned int ramstart, unsigned int ramend, unsigned int length)
{
	buffer = new unsigned short [(BLOCKSIZE + 1) / 2];
	buffersize = 0;
	bufferpos = 0;
	
	board = b;
	datastart = ramstart;
	databuffersize = ramend - ramstart;
	dataend = datastart + length;
	dataptr = datastart;
}

RAMRawDataReader::~RAMRawDataReader()
{
	if (buffer)
		delete buffer;
}

PipeObjectShort * RAMRawDataReader::Write()
{
	if (buffersize > 0 && bufferpos < buffersize) {
		/* there is unread data in the buffer */
		s.s = buffer[bufferpos++];
		return &s;
	} else if (buffersize > 0 && bufferpos >= buffersize || buffersize == 0) {
		/* All data was read from the buffer or buffer empty. Get new data from the RAM */
		
		cout << "> Megabytes left to read: " << (dataend - dataptr) / 1024 / 1024 << " \r";
		cout.flush();
		
		/* Check whether there is data left to read */
		if (dataptr >= dataend) {
			cout << "                              \r";
			cout.flush();
			return NULL;
		}
		
		/* Read blocks of BLOCKSIZE bytes. */
		unsigned int bytes = (dataend - dataptr < BLOCKSIZE) ? (dataend - dataptr) : BLOCKSIZE;
		
		/* Reading smaller amounts leads to corrupt data for some reason. */
		unsigned int offset = 0;
		if (bytes < BLOCKSIZE && databuffersize > BLOCKSIZE) {
			if (dataptr != datastart)
				offset = BLOCKSIZE - bytes;
			bytes = BLOCKSIZE;
		}
		
		/* Read the data */
		board->Flush();
		board->Clear();
		memset(buffer, BLOCKSIZE, '0');
		board->MemRead(dataptr - offset, bytes, (unsigned char *) buffer);
		board->Flush();
		board->Clear();
		dataptr += bytes - offset;
		if ((bytes % 2) == 1)
			cout << "reading odd number of bytes!" << endl;
		if ((offset % 2) == 1)
			cout << "offset is odd number!" << endl;
		buffersize = bytes / 2;
		bufferpos = offset / 2;
		s.s = buffer[bufferpos++];
		return &s;
	}
}

/* Pipe that brakes raw data into raw events --------------------------------- */

PipeObjectShort * RawData2RawEvent::Read()
{
	return static_cast<PipeObjectShort *>(source->Write());
}

CRawEvent * RawData2RawEvent::Write()
{
	/* This variable keeps its value between calls to the function. In the beginning it has the value NULL. */
	static PipeObjectShort * last = NULL;
	
	/* Fetch a value for the first time */
	if (!last)
		last = Read();

	/* If GetNext() returns NULL, it's the last entry */
	if (!last)
		return NULL;
		
	/* Read the event header */
	rawevent.flags = last->s;
	
	/* Check header validity */
	if (!rawevent.IsHeaderOk()) {
		cout << "RawData2RawEvent: Header expected, but no header found!" << endl;
		return NULL;
	}

	/* Read the timestamp (number of clockcycles, 48 bit) */
	if (!(last = Read()))
		return NULL;
	rawevent.time = last->s;
	if (!(last = Read()))
		return NULL;
	rawevent.time = (rawevent.time<<16) | last->s;
	if (!(last = Read()))
		return NULL;
	rawevent.time = (rawevent.time<<16) | last->s;

	/* Read data */
	if (!(last = Read()))
		return NULL;
	int d = last->s;
	rawevent.length = 0;
	while (!(d & 0x8000)) {
		if (rawevent.length < MAXDATASIZE) {
			/* remove the data header and extend the sign */
			rawevent.data[rawevent.length]
				= (d & 0x0800) ? (d & 0x0fff) - 4096 : (d & 0xfff);
			rawevent.dflag[rawevent.length] = (d >> 12) & 7;
			rawevent.length++;
		} else {
			/* Buffer overflow */
			cout << endl << "RawData2RawEvent: Buffer overflow" << endl;
			return NULL;
		}
		/* If this is the last entry in the buffer, then complete the raw event and return it. The
		   next call to this function will attempt to get the next event, and then will fail. */
		if (!(last = Read()))
			break;
		d = last->s;
	}
	
	return &rawevent;
}

/* Pipe that decodes the analog readout from raw events ------------------------------------------------- */

RawEventDecoder::RawEventDecoder(unsigned int n)
{
	nROCs = n;
}

CRawEvent * RawEventDecoder::Read()
{
	return static_cast<CRawEvent *>(source->Write());
}

CEvent * RawEventDecoder::Write()
{
	CRawEvent * rawevent;
	rawevent = Read();
	if (!rawevent) {
		return NULL;
	}
	decoded_event.isData = rawevent->IsData();
	decoded_event.isTrigger = rawevent->IsTrigger();
	decoded_event.isReset = rawevent->IsRocReset();
	decoded_event.isCalibrate = rawevent->IsCalibrate();
	decoded_event.isOverflow = rawevent->IsOverflow();
	decoded_event.timestamp = rawevent->time;
	decoded_event.nRocs = nROCs;
	decoded_event.nHits = 0;
	
	/* Decode the analog data, if available */
	if (decoded_event.isData && rawevent->length > 0) {
		RawPacketDecoder * decoder = RawPacketDecoder::Singleton();
		decoded_event.nHits = decoder->decode(rawevent->length, (short *) rawevent->data, decoded_event.hits, nROCs);
		/* go through the hits to find address decoding errors */
		if (decoded_event.nHits < 0) {
			for (int q = 0; q < rawevent->length; q++) {
				cout << rawevent->data[q] << " ";
			}
			cout << endl;
		}
	}

	return &decoded_event;
}

/* Filter pipe that stores hits in a 2D histogram ----------------------------------------------------------- */

HitMapper::HitMapper()
{
	hitmap = new TH2I("hitmap", "Pixel hit map", 52, 0, 52, 80, 0, 80);
}

HitMapper::~HitMapper()
{
	delete hitmap;
}

CEvent * HitMapper::Read()
{
	return static_cast<CEvent *>(source->Write());
}

CEvent * HitMapper::Write()
{
	CEvent * event = Read();
	if (event) {
		if (event->nHits > 0) {
			for (int r = 0; r < event->nRocs; r++) {
				int h = event->hits.roc[r].numPixelHits;
				for (int i = 0; i < h; i++) {
					hitmap->Fill(event->hits.roc[r].pixelHit[i].columnROC, event->hits.roc[r].pixelHit[i].rowROC);
				}
			}
		}
	}
	return event;
}

TH2I * HitMapper::getHitMap(unsigned int iroc)
{
	return hitmap;
}

/* Filter pipe that stores hits in a 2D histogram ------------------------------------------------------------ */

EfficiencyMapper::EfficiencyMapper(unsigned int ntrig)
{
	effmap = new TH2I("effmap", "Pixel efficiency map", 52, 0, 52, 80, 0, 80);
	bkgmap = new TH2I("bkgmap", "Pixel efficiency background map", 52, 0, 52, 80, 0, 80);
	effdist = new TH1I("effdist", "Pixel efficiency distribution", 101, 0, 101);
	TrigPerPixel = ntrig;
	EventCounter = 0;
}

EfficiencyMapper::~EfficiencyMapper()
{
	delete effmap;
	delete bkgmap;
	delete effdist;
}

CEvent * EfficiencyMapper::Read()
{
	return static_cast<CEvent *>(source->Write());
}

CEvent * EfficiencyMapper::Write()
{
	CEvent * event = Read();
	if (event) {
		int testcol = -1, testrow = -1;
		if (TrigPerPixel > 0 && event->isData) {
			testcol = EventCounter / (TrigPerPixel * 80);
			testrow = (EventCounter % (TrigPerPixel * 80)) / TrigPerPixel;
			EventCounter++;
		}
		if (event->nHits > 0) {
			for (int r = 0; r < event->nRocs; r++) {
				int h = event->hits.roc[r].numPixelHits;
				for (int i = 0; i < h; i++) {
					if (event->hits.roc[r].pixelHit[i].columnROC == testcol && event->hits.roc[r].pixelHit[i].rowROC == testrow)
						effmap->Fill(event->hits.roc[r].pixelHit[i].columnROC, event->hits.roc[r].pixelHit[i].rowROC);
					else
						bkgmap->Fill(event->hits.roc[r].pixelHit[i].columnROC, event->hits.roc[r].pixelHit[i].rowROC);
				}
			}
		}
	}
	return event;
}

TH2I * EfficiencyMapper::getEfficiencyMap(unsigned int iroc)
{
	return effmap;
}

TH1I * EfficiencyMapper::getEfficiencyDist(unsigned int iroc)
{
	effdist->Clear();
	effdist->SetNameTitle("effdist", "Pixel efficiency distribution");
	for (int col = 0; col < 52; col++) {
		for (int row = 0; row < 80; row++) {
			effdist->Fill(effmap->GetBinContent(col + 1, row + 1) * 100.0 / TrigPerPixel);
		}
	}
	return effdist;
}

TH2I * EfficiencyMapper::getBackgroundMap(unsigned int iroc)
{
	return bkgmap;
}

/* Filter pipe which counts things related to events --------------------------------------------------------------- */

EventCounter::EventCounter()
{
	haveTrigger = false;
	TriggerCounter = 0;
	RocSequenceErrorCounter = 0;
}

CEvent * EventCounter::Read()
{
	return static_cast<CEvent *>(source->Write());
}

CEvent * EventCounter::Write()
{
	CEvent * ev = Read();
	if (!ev)
		return ev;

	if (ev->nHits == -4)
		RocSequenceErrorCounter++;
	if (ev->isTrigger)
		haveTrigger = true;
	if (ev->isData) {
		if (haveTrigger)
			TriggerCounter++;
		else
			cout << "EventCounter: Data event without preceding trigger!" << endl;
		haveTrigger = false;
	}

	return ev;
}

/* Filter pipe that histograms the number of hits per trigger ------------------------------------------------------- */

MultiplicityHistogrammer::MultiplicityHistogrammer()
{
	ModuleMultiplicity = new TH1I("module_multiplicity", "Module hit multiplicity", 500, 0, 500);
	nRocs = -1;
	for (int roc = 0; roc < 16; roc++) {
		RocMultiplicity[roc] = NULL;
		for (int dcol = 0; dcol < 26; dcol++) {
			DColMultiplicity[roc][dcol] = NULL;
		}
	}
}

MultiplicityHistogrammer::~MultiplicityHistogrammer()
{
	delete ModuleMultiplicity;
	for (int roc = 0; roc < nRocs; roc++) {
		delete RocMultiplicity[roc];
		for (int dcol = 0; dcol < 26; dcol++)
			delete DColMultiplicity[roc][dcol];
	}
}

CEvent * MultiplicityHistogrammer::Read()
{
	return static_cast<CEvent *>(source->Write());
}

CEvent * MultiplicityHistogrammer::Write()
{
	CEvent * ev = Read();
	if (!ev)
		return NULL;

	/* initialise histograms if not already done (depends on number of ROCs) */
	if (nRocs == -1) {
		nRocs = ev->nRocs;
		for (int roc = 0; roc < ev->nRocs; roc++) {
			RocMultiplicity[roc] = new TH1I(Form("roc_multiplicity_%i", roc), Form("ROC %i hit multiplicity", roc), 40, 0, 40);
			for (int dcol = 0; dcol < 26; dcol++)
				DColMultiplicity[roc][dcol] = new TH1I(Form("dcol_multiplicity_%i_%i", roc, dcol), Form("DCol %i hit multiplicity (ROC %i)", dcol, roc), 15, 0, 15);
		}
	}

	if (!(ev->isData && ev->nHits >= 0))
		return ev;

	ModuleMultiplicity->Fill(ev->nHits);
	for (int roc = 0; roc < ev->nRocs && roc < nRocs; roc++) {
		int h = ev->hits.roc[roc].numPixelHits;
		RocMultiplicity[roc]->Fill(h);
		int dcolhits [26] = {0};
		for (int i = 0; i < h; i++) {
			dcolhits[ev->hits.roc[roc].pixelHit[i].columnROC / 2]++;
		}
		for (int dcol = 0; dcol < 26; dcol++) {
			DColMultiplicity[roc][dcol]->Fill(dcolhits[dcol]);
		}
	}
	
	return ev;
}

TH1I * MultiplicityHistogrammer::getModuleMultiplicity()
{
	return ModuleMultiplicity;
}

TH1I * MultiplicityHistogrammer::getRocMultiplicity(int roc)
{
	if (roc < nRocs)
		return RocMultiplicity[roc];

	return NULL;
}

TH1I * MultiplicityHistogrammer::getDColMultiplicity(int roc, int dcol)
{
	if (roc < nRocs)
		if (dcol >= 0 && dcol < 26)
			return DColMultiplicity[roc][dcol];
	
	return NULL;
}

/* Pipe that histograms the pulse height ------------------------------------------------------------------- */

PulseHeightHistogrammer::PulseHeightHistogrammer()
{
	pulseheight = new TH1I("pulseheight", "Pulse height", 2048, -1024, 1024);
}

PulseHeightHistogrammer::~PulseHeightHistogrammer()
{
	delete pulseheight;
}

CEvent * PulseHeightHistogrammer::Read()
{
	return static_cast<CEvent *>(source->Write());
}

CEvent * PulseHeightHistogrammer::Write()
{
	CEvent * ev = Read();
	if (!ev)
		return NULL;

	if (!(ev->isData && ev->nHits >= 0))
		return ev;

	for (int roc = 0; roc < ev->nRocs; roc++) {
		int h = ev->hits.roc[roc].numPixelHits;
		for (int i = 0; i < h; i++) {
			pulseheight->Fill(ev->hits.roc[roc].pixelHit[i].analogPulseHeight);
		}
	}

	return ev;
}

TH1I * PulseHeightHistogrammer::getPulseHeightHistogram()
{
	return pulseheight;
}
