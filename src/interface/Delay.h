// Class provides basic delay and time analysis functionalities
// The delays occur on the PC, not on the testboard

#ifndef DELAY
#define DELAY

typedef unsigned long long int QWORD;

class Delay {

 public:
	Delay();

// == Delays ================================================
	void Mdelay(unsigned int ms);
	void Udelay(unsigned long us);

// == Calibration ===========================================
	int Median(unsigned int n, int x[]);
	int Cpu_MHz();
	int Calibrate();
	int GetCPU_MHz();
	unsigned long Usecs();
	QWORD Rdtsc();

// == Runtime analysis =======================================
	void Timestamp();
	void StartUsecs();
	void PrintUsecs();
	
 protected:

 	 unsigned long uscount, usecsStart;
 
};


static Delay* gDelay = new Delay();

#endif
