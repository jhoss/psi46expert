#include <iostream>
#include <iomanip>

#include <TSystem.h>
#include <TRandom.h>
#include <TMath.h>

#include "interface/Delay.h"
#include "BasePixel/Roc.h"
#include "BasePixel/TBAnalogInterface.h"
#include "BasePixel/ConfigParameters.h"
#include "interface/Log.h"
#include "TestRoc.h"
#include "DacDependency.h"
#include "PHCalibration.h"
#include "pipe.h"
#include "DataFilter.h"
#include "BasePixel/DigitalReadoutDecoder.h"
#include "BasePixel/DecodedReadout.h"
#include "TestModule.h"


PHCalibration::PHCalibration()
{
    psi::LogDebug() << "[PHCalibration] Initialization." << psi::endl;

    Initialize();
}


PHCalibration::PHCalibration(TestRange * aTestRange, TestParameters * aTestParameters, TBInterface * aTBInterface)
{
    testParameters = aTestParameters;
    testRange = aTestRange;
    tbInterface = aTBInterface;
    ReadTestParameters(testParameters);
    Initialize();
}


void PHCalibration::Initialize()
{
    //  printf("mode %i\n", mode);s
    if (mode == 0)
    {
        vcalSteps = 10;
        vcal[0] = 50;
        vcal[1] = 100;
        vcal[2] = 150;
        vcal[3] = 200;
        vcal[4] = 250;
        vcal[5] = 30;
        vcal[6] = 50;
        vcal[7] = 70;
        vcal[8] = 90;
        vcal[9] = 200;
        for (int i = 0; i < 5; i++)  ctrlReg[i] = 0;
        for (int i = 5; i < 10; i++)  ctrlReg[i] = 4;
    }
    else if (mode == 1)
    {
        vcalSteps = 100;
        for (int i = 0; i < vcalSteps; i++)
        {
            vcal[i] = 5 + i * 5;
            vcal[i + vcalSteps] = 5 + i * 5;
            if (i < 50) ctrlReg[i] = 0; else ctrlReg[i] = 4;
        }
    }
    else if (mode == 2)
    {
        vcalSteps = 102;
        for (int i = 0; i < 51; i++)
        {
            vcal[i] = 5 + i * 5;
            ctrlReg[i] = 0;
        }
        for (int i = 51; i < 102; i++)
        {
            vcal[i] = 5 + (i - 51) * 5;
            ctrlReg[i] = 4;
        }
    }
    if (mode == 3)
    {
        vcalSteps = 9;
        vcal[0] = 50;
        vcal[1] = 100;
        vcal[2] = 150;
        vcal[3] = 200;
        vcal[4] = 250;
        vcal[5] = 50;
        vcal[6] = 70;
        vcal[7] = 90;
        vcal[8] = 200;
        for (int i = 0; i < 5; i++)  ctrlReg[i] = 0;
        for (int i = 5; i < 9; i++)  ctrlReg[i] = 4;
    }
}


void PHCalibration::ReadTestParameters(TestParameters * testParameters)
{
    nTrig = (*testParameters).PHCalibrationNTrig;
    //  memoryCorrection = (*testParameters).PHMemoryCorrection / 100;
    mode = (*testParameters).PHCalibrationMode;
    numPixels = (*testParameters).PHCalibrationNPixels;
    adjustVthrComp = (*testParameters).PHCalibrationAdjustVthrComp;
    adjustCalDel = (*testParameters).PHCalibrationAdjustCalDel;
}


void PHCalibration::RocAction()
{
    psi::LogInfo() << "[PHCalibration] Chip #" << chipId << " Calibration: start." << psi::endl;

    gDelay->Timestamp();
    SaveDacParameters();

    // == Open file

    ConfigParameters * configParameters = ConfigParameters::Singleton();
    char fname[1000];
    sprintf(fname, "%s/phCalibration_C%i.dat", configParameters->directory, chipId);
    FILE * file = fopen(fname, "w");
    if (!file)
    {
        psi::LogInfo() << "[PHCalibration] Error: Can not open file '" << fname
                       << "' to write PH Calibration." << psi::endl;

        return;
    }

    psi::LogInfo() << "[PHCalibration] Writing PH Calibration to '" << fname
                   << "'." << psi::endl;

    fprintf(file, "Pulse heights for the following Vcal values:\n");
    fprintf(file, "Low range: ");
    for (int i = 0; i < vcalSteps; i++)
    {
        if (ctrlReg[i] == 0) fprintf(file, "%3i ", vcal[i]);
    }
    fprintf(file, "\n");
    fprintf(file, "High range: ");
    for (int i = 0; i < vcalSteps; i++)
    {
        if (ctrlReg[i] == 4) fprintf(file, "%3i ", vcal[i]);
    }
    fprintf(file, "\n");
    fprintf(file, "\n");

    int numFlagsRemaining = numPixels;
    TRandom u;
    bool pxlFlags[ROCNUMROWS * ROCNUMCOLS];
    if (numPixels < 4160)
    {
        while (numFlagsRemaining > 0) {
            int column = TMath::FloorNint(ROCNUMCOLS * u.Rndm());
            int row    = TMath::FloorNint(ROCNUMROWS * u.Rndm());

            if (pxlFlags[column * ROCNUMROWS + row] == false) { // pixel not yet included in test
                cout << "flagging pixel in column = " << column << ", row = " << row << " for testing" << endl;
                pxlFlags[column * ROCNUMROWS + row] = true;
                numFlagsRemaining--;
            }
        }
    }

    int original_VthrComp = GetDAC("VthrComp");
    int original_CalDel = GetDAC("CalDel");

    // Determine VthrComp values

    if (adjustVthrComp)
    {
        psi::LogInfo() << "[PHCalibration] Determining VthrComp values ..." << psi::endl;
        SetDAC("CtrlReg", 0);

        // Values for Vcal 200
        // Assume we have these values from the pretest
        // roc->AdjustCalDelVthrComp(15, 15, 200, -1);
        vthrComp200 = GetDAC("VthrComp");

        // Values for Vcal 50
        roc->AdjustCalDelVthrComp(15, 15, 50, -0);
        vthrComp50 = GetDAC("VthrComp");

        // Values for Vcal 100
        roc->AdjustCalDelVthrComp(15, 15, 100, -0);
        vthrComp100 = GetDAC("VthrComp");
    }
    else
    {
        // Do not adjust the threshold, take the values as they are
        vthrComp50 = vthrComp100 = vthrComp200 = GetDAC("VthrComp");
    }

    // == Loop over all pixels

    int ph[vcalSteps][ROCNUMROWS * ROCNUMCOLS];
    int data[ROCNUMROWS * ROCNUMCOLS];
    int phPosition = 16 + aoutChipPosition * 3;

    for (int i = 0; i < vcalSteps; i++)
    {
        SetDAC("CtrlReg", ctrlReg[i]);
        SetDAC("CalDel", original_CalDel);
        SetDAC("VthrComp", GetVthrComp(i));
        SetDAC("Vcal", vcal[i]);
        Flush();

        if (adjustCalDel)
            roc->AdjustCalDel(0);

        psi::LogInfo() << "[PHCalibration] Measuring pulse height with Vcal ";
        psi::LogInfo() << Form("%3i", vcal[i]) << (ctrlReg[i] == 4 ? "H" : "L") << " ... " << psi::endl;

        if (numPixels >= 4160) {
            if (roc->has_analog_readout())
                roc->AoutLevelChip(phPosition, nTrig, data);
            else
                PulseHeightRocDigital(data);
        } else {
            if (roc->has_analog_readout())
                roc->AoutLevelPartOfChip(phPosition, nTrig, data, pxlFlags);
            else
                PulseHeightRocDigital(data);
        }

        for (int k = 0; k < ROCNUMROWS * ROCNUMCOLS; k++) ph[i][k] = data[k];
    }

    for (int col = 0; col < 52; col++)
    {
        for (int row = 0; row < 80; row++)
        {
            SetPixel(GetPixel(col, row));
            if (testRange->IncludesPixel(chipId, column, row))
            {

                for (int i = 0; i < vcalSteps; i++)
                {
                    if (ph[i][col * ROCNUMROWS + row] != 7777) fprintf(file, "%5i ", ph[i][col * ROCNUMROWS + row]);
                    else fprintf(file, "  N/A ");
                }
                fprintf(file, "   Pix %2i %2i\n", col, row);
            }
        }
    }

    for (int i = 0; i < vcalSteps; i++) {
        TH2F * ph_map = new TH2F(Form("ph_cal_map_vcal%i%s_C%i", vcal[i], ctrlReg[i] == 4 ? "H" : "L", chipId), Form("Pulse height calibration map ROC %i (Vcal %i%s)", chipId, vcal[i], ctrlReg[i] == 4 ? "H" : "L"), 52, 0, 52, 80, 0, 80);
        ph_map->GetXaxis()->SetTitle("Column");
        ph_map->GetYaxis()->SetTitle("Row");

        for (int col = 0; col < 52; col++) {
            for (int row = 0; row < 80; row++) {
                SetPixel(GetPixel(col, row));
                if (testRange->IncludesPixel(chipId, column, row) && ph[i][col * ROCNUMROWS + row] != 7777) {
                    ph_map->SetBinContent(col + 1, row + 1, ph[i][col * ROCNUMROWS + row]);
                    //if (ph[i][col*ROCNUMROWS + row] != 7777) fprintf(file, "%5i ", ph[i][col*ROCNUMROWS + row]);
                    //else fprintf(file, "  N/A ");
                }
            }
        }

        histograms->Add(ph_map);
    }

    fclose(file);
    RestoreDacParameters();
    gDelay->Timestamp();
}


int PHCalibration::GetVthrComp(int vcalStep)
{
    int conversion = 1;
    if (ctrlReg[vcalStep] == 4) conversion = 7;
    if (vcal[vcalStep]*conversion < 75.) return vthrComp50;
    else if (vcal[vcalStep]*conversion < 125.) return vthrComp100;
    else return vthrComp200;
}

void PHCalibration::PulseHeightRocDigital(int data [])
{

    TBAnalogInterface * ai = (TBAnalogInterface *) tbInterface;
    ai->Flush();

    for (int col = 0; col < 52; col++) {
        for (int row = 0; row < 80; row++) {
            data[80 * col + row] = ai->PH(col, row);
        }
    }

    return;
}
