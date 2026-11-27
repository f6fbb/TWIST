/* deep_calib.c - largest deep-search Z on pure noise, per run, to calibrate
 * the effective number of hypotheses in twc_deep_search().
 * usage: deep_calib narrow(0|1) trials ncalls(1..10) ; prints "maxZ z(fa=1)" per run */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "twbb.h"
#include "twsync.h"
#include "twcdeep.h"
#include "synth.h"
int main(int argc,char**argv){ if(argc<4){fprintf(stderr,"usage: deep_calib narrow trials ncalls\n");return 2;}
  int narrow=atoi(argv[1]), trials=atoi(argv[2]); int nk=atoi(argv[3]);
  const char *cl[]={"F6FBB","F1HDI","F5ZZO","OZ7IGY","EA5XYZ","G4JNT","DL0ABC","K1JT","9A1A","VK2ABC"};
  twc_known_t kn[10]; for(int i=0;i<nk;i++) twc_known_init(cl[i],&kn[i]);
  int fs=12000; double T=0.128; long N=(long)((239*T+6)*fs); double *x=malloc(8*N); synth_seed(4242+narrow);
  for(int t=0;t<trials;t++){ for(long k=0;k<N;k++) x[k]=0.1*synth_gauss();
    tw_bb_t bb; tw_spec_t sp; twc_deephit_t h[4]; double zth;
    double fc = narrow? 1500:1500, tol = narrow? 8:200;
    tw_baseband_real(x,N,fs,fc,(narrow?125:500),&bb); tw_spectrogram(&bb,T,tol,0.0,&sp);
    double d0 = narrow? 2.0:-1e9, d1 = narrow? 3.0:1e9;
    int nh=twc_deep_search(&sp,kn,nk,d0,d1,tol,0.2,1e9,4,h,&zth);
    /* also report the model's n_hyp via threshold at fa=1 */
    double z1; twc_deep_search(&sp,kn,nk,d0,d1,tol,0.2,1.0,0,h+3,&z1);
    printf("%.3f %.3f\n", nh>0?h[0].z:0, z1);
    tw_spectrogram_free(&sp); tw_baseband_free(&bb);}
}
