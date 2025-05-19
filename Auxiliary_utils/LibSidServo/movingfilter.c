#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define NFILT	(5)

static double filterK[NFILT];

static void buildFilter(){
	filterK[NFILT-1] = 1.;
	double sum = 1.;
	for(int i = NFILT-2; i > -1; --i){
		filterK[i] = (filterK[i+1] + 1.) * 1.1;
		sum += filterK[i];
	}
	for(int i = 0; i < NFILT; ++i){
		filterK[i] /= sum;
		fprintf(stderr, "%d: %g\n", i, filterK[i]);
	}
}

static double filter(double val){
	static int ctr = 0;
	static double lastvals[NFILT] = {0.};
	for(int i = NFILT-1; i > 0; --i) lastvals[i] = lastvals[i-1];
	lastvals[0] = val;
	double r = 0.;
	if(ctr < NFILT){
		++ctr;
		return val;
	}
	for(int i = 0; i < NFILT; ++i) r += filterK[i] * lastvals[i];
	return r;
}

int main(int argc, char **argv){
	buildFilter();
	printf("Signal\tNoiced\tFiltered\n");
	for(int i = 0; i < 100; ++i){
		double di = (double)i;
		double sig = di * di / 1e5 + sin(i * M_PI / 1500.);
		double noiced = sig + 0.1 * (drand48() - 0.5);
		printf("%.3f\t%.3f\t%.3f\n", sig, noiced, filter(noiced));
	}
	return 0;
}