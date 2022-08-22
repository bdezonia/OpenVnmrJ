/*
 * Copyright (C) 2015  University of Oregon
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Apache License, as specified in the LICENSE file.
 *
 * For more information, see the LICENSE file.
 */

/***********************************************************************
Gradient echo imaging sequence
************************************************************************/

#include <standard.h>
#include "sgl.c"

void pulsesequence()
{
  /* Internal variable declarations *********************/
  double  freqEx[MAXNSLICE],tearray[500];
  double  maxgradtime,perTime,tetime,trtime,te_delay,tr_delay;
  double  spoilMoment,maxtearray;
  int     i,table,shapeEx,tesize,sepSliceRephase;
  char    spoilflag[MAXSTR],mriout[MAXSTR],slprofile[MAXSTR];

  /* Real-time variables used in this sequence **********/
  int  vpe_steps    = v1;      // Number of PE steps
  int  vpe_ctr      = v2;      // PE loop counter
  int  vms_slices   = v3;      // Number of slices
  int  vms_ctr      = v4;      // Slice loop counter
  int  vpe_offset   = v5;      // PE/2 for non-table offset
  int  vpe_mult     = v6;      // PE multiplier, ranges from -PE/2 to PE/2
  int  vper_mult    = v7;      // PE rewinder multiplier; turn off rewinder when 0
  int  vssc         = v8;      // Compressed steady-states
  int  vacquire     = v9;      // Argument for setacqvar, to skip steady state acquires
  int  vrfspoil_ctr = v10;     // RF spoil counter
  int  vrfspoil     = v11;     // RF spoil multiplier
  int  vtrimage     = v12;     // Counts down from nt, trimage delay when 0
  int  vtrigblock   = v13;     // Number of slices per trigger block

  /* Initialize paramaters ******************************/
  init_mri();
  getstr("spoilflag",spoilflag);
  getstrnwarn("mriout",mriout);
  getstrnwarn("slprofile",slprofile);

  /*  Check for external PE table ***********************/
  table = 0;
  if (strcmp(petable,"n") && strcmp(petable,"N") && strcmp(petable,"")) {
    loadtable(petable);
    table = 1;
  }

  /* Set Rcvr/Xmtr phase increments for RF Spoiling *****/
  /* Ref:  Zur, Y., Magn. Res. Med., 21, 251, (1991) ****/
  if (rfspoil[0] == 'y') {
    rcvrstepsize(rfphase);
    obsstepsize(rfphase);
  }

  /* RF Calculations ************************************/
  shape_rf(&p1_rf,"p1",p1pat,p1,flip1,rof1,rof2);  // excitation pulse
  calc_rf(&p1_rf,"tpwr1","tpwr1f");

  /* Initialize gradient structures *********************/
  init_slice(&ss_grad,"ss",thk);                 // slice gradient
  init_slice_refocus(&ssr_grad,"ssr");           // slice refocus gradient
  init_readout(&ro_grad,"ro",lro,np,sw);         // readout gradient
  init_readout_refocus(&ror_grad,"ror");         // dephase gradient
  init_phase(&pe_grad,"pe",lpe,nv);              // phase encode gradient
  init_dephase(&spoil_grad,"spoil");             // Optimized spoiler

  /* Gradient calculations ******************************/
  calc_readout(&ro_grad, WRITE, "gro","sw","at");
  calc_readout_refocus(&ror_grad, &ro_grad, NOWRITE, "gror");
  calc_phase(&pe_grad, NOWRITE, "gpe","tpe");
  calc_slice(&ss_grad,&p1_rf,WRITE,"gss");
  if (slprofile[0] == 'y')
    ss_grad.m0ref += ro_grad.m0ref;
  calc_slice_refocus(&ssr_grad,&ss_grad,WRITE,"gssr");

  spoilMoment = ro_grad.acqTime*ro_grad.roamp;   // Optimal spoiling is at*gro for 2pi per pixel
  spoilMoment -= ro_grad.m0def;                  // Subtract partial spoiling from back half of readout
  calc_dephase(&spoil_grad,WRITE,spoilMoment,"gspoil","tspoil");

  /* Is TE long enough for separate slab refocus? *******/
  maxgradtime = MAX(ror_grad.duration,pe_grad.duration);
  if (spoilflag[0] == 'y')
    maxgradtime = MAX(maxgradtime,spoil_grad.duration);
  tetime = ss_grad.rfCenterBack + ssr_grad.duration + maxgradtime + alfa + ro_grad.timeToEcho + 4e-6;

  /* Equalize refocus and PE gradient durations *********/
  if ((te >= tetime) && (minte[0] != 'y')) {
    sepSliceRephase = 1;                         // Set flag for separate slice rephase
    calc_sim_gradient(&ror_grad,&pe_grad,&spoil_grad,tpemin,WRITE);
  } else {
    sepSliceRephase = 0;
    calc_sim_gradient(&ror_grad,&pe_grad,&ssr_grad,tpemin,WRITE);
    calc_sim_gradient(&ror_grad,&spoil_grad,&null_grad,tpemin,NOWRITE);
  }
  putvalue("tspoil",spoil_grad.duration);
  putvalue("gspoil",spoil_grad.amp);

  perTime = 0.0;
  if ((perewind[0] == 'y') || (spoilflag[0] == 'y'))
    perTime = spoil_grad.duration;
  if (spoilflag[0] == 'n')
    spoil_grad.amp = 0.0;

  /* Create optional prepulse events ********************/
  if (fsat[0] == 'y') create_fatsat();
  if (sat[0] == 'y')  create_satbands();
  if (mt[0] == 'y')   create_mtc();
  if (ir[0] == 'y')   create_inversion_recovery();
  
  /* Check that all Gradient calculations are ok ********/
  sgl_error_check(sglerror);

  /* Min TE *********************************************/
  tetime = ss_grad.rfCenterBack + pe_grad.duration + alfa + ro_grad.timeToEcho;
  tetime += (sepSliceRephase) ? ssr_grad.duration : 0.0;   // Add slice refocusing if separate event

  tesize = getarray("te",tearray);
  maxtearray = tearray[0];
  for (i=1; i<tesize; i++) {
    maxtearray = tearray[i] > maxtearray ? tearray[i] : maxtearray;
  }

  temin = tetime + 4e-6;  /* ensure that te_delay is at least 4us */
  if (minte[0] == 'y') {
    te = temin;
    maxtearray = te;                             // use the new temin value, instead of previously determined maximum
    putvalue("te",te);
  }
  if (te < temin) {
    abort_message("TE too short.  Minimum TE= %.2fms\n",temin*1000+0.005);   
  }
  te_delay = te - tetime;

  /* Check nsblock, the number of slices blocked together
     (used for triggering and/or inversion recovery) */
  check_nsblock();

  /* Min TR *********************************************/   	
  trmin  = ss_grad.duration + te_delay + pe_grad.duration + ro_grad.duration + perTime + 8e-6;
  trmin += (sepSliceRephase) ? ssr_grad.duration : 0.0;   // Add slice refocusing if separate event
  trmin += (maxtearray - te);

  /* Increase TR if any options are selected ************/
  if (sat[0] == 'y')  trmin += satTime;
  if (fsat[0] == 'y') trmin += fsatTime;
  if (mt[0] == 'y')   trmin += mtTime;
  if (ticks > 0) trmin += 4e-6;

  /* Adjust for all slices ******************************/
  trmin *= ns;

  /* Inversion recovery *********************************/
  if (ir[0] == 'y') {
    /* tiaddTime is the additional time beyond IR component to be included in ti */
    /* satTime, fsatTime and mtTime all included as those modules will be after IR */
    tiaddTime = satTime + fsatTime + mtTime + 4e-6 + ss_grad.rfCenterFront;
    /* calc_irTime checks ti and returns the time of all IR components */
    trmin += calc_irTime(tiaddTime,trmin,mintr[0],tr,&trtype);
  }

  if (mintr[0] == 'y') {
    tr = trmin;
    putvalue("tr",tr);
  }
  if (FP_LT(tr,trmin)) {
    abort_message("TR too short.  Minimum TR = %.2fms\n",trmin*1000+0.005);
  }

  /* Calculate tr delay */
  tr_delay = granularity((tr-trmin)/ns,GRADIENT_RES);

  /* Set up frequency offset pulse shape list ***********/   	
  offsetlist(pss,ss_grad.ssamp,0,freqEx,ns,seqcon[1]);
  shapeEx = shapelist(p1_rf.pulseName,ss_grad.rfDuration,freqEx,ns,ss_grad.rfFraction,seqcon[1]);
  
  /* Set pe_steps for profile or full image *************/   	
  pe_steps = prep_profile(profile[0],nv,&pe_grad,&per_grad);
  F_initval(pe_steps/2.0,vpe_offset);

  /* Slice profile **************************************/   	
  if (slprofile[0] == 'y') ror_grad.amp = 0;

  /* Shift DDR for pro **********************************/   	
  roff = -poffset(pro,ro_grad.roamp);

  /* Adjust experiment time for VnmrJ *******************/
  if (ssc<0) {
    if (seqcon[2] == 'c') g_setExpTime(trmean*(ntmean*pe_steps*arraydim - ssc*arraydim));
    else g_setExpTime(trmean*(ntmean*pe_steps*arraydim - ssc*pe_steps*arraydim));
  }
  else g_setExpTime(trmean*ntmean*pe_steps*arraydim + tr*ssc);

  /* PULSE SEQUENCE *************************************/
  status(A);
  rotate();                          // Initialize default orientation
  triggerSelect(trigger);            // Select trigger input 1/2/3
  obsoffset(resto);
  delay(4e-6);
  initval(fabs(ssc),vssc);           // Compressed steady-state counter
  if (seqcon[2]=='s') assign(zero,vssc); // Zero for standard peloop
  assign(zero,vrfspoil_ctr);         // RF spoil phase counter
  assign(zero,vrfspoil);             // RF spoil multiplier
  assign(one,vacquire);              // real-time acquire flag
  setacqvar(vacquire);               // Turn on acquire when vacquire is zero 

  /* trigger */
  if (ticks > 0) F_initval((double)nsblock,vtrigblock);

  /* Begin phase-encode loop ****************************/       
  peloop(seqcon[2],pe_steps,vpe_steps,vpe_ctr);

    if (trtype) delay(ns*tr_delay);  // relaxation delay

    /* Compressed steady-states: 1st array & transient, all arrays if ssc is negative */
    if ((ix > 1) && (ssc > 0))
      assign(zero,vssc);
    sub(vpe_ctr,vssc,vpe_ctr);       // vpe_ctr counts up from -ssc
    assign(zero,vssc);
    if (seqcon[2] == 's')
      assign(zero,vacquire);         // Always acquire for non-compressed loop
    else {
      ifzero(vpe_ctr);
        assign(zero,vacquire);       // Start acquiring when vpe_ctr reaches zero
      endif(vpe_ctr);
    }

    /* Set rcvr/xmtr phase for RF spoiling ****************/
    if (rfspoil[0] == 'y') {
      incr(vrfspoil_ctr);                   // vrfspoil_ctr = 1  2  3  4  5  6
      add(vrfspoil,vrfspoil_ctr,vrfspoil);  // vrfspoil =     1  3  6 10 15 21
      xmtrphase(vrfspoil);
      rcvrphase(vrfspoil);
    }

    /* Read external kspace table if set ******************/       
    if (table)
      getelem(t1,vpe_ctr,vpe_mult);
    else {
      ifzero(vacquire);
        sub(vpe_ctr,vpe_offset,vpe_mult);
      elsenz(vacquire);
        sub(zero,vpe_offset,vpe_mult);      // Hold PE mult at initial value for steady states
      endif(vacquire);
    }

    /* PE rewinder follows PE table; zero if turned off ***/       
    if (perewind[0] == 'y')
      assign(vpe_mult,vper_mult);
    else
      assign(zero,vper_mult);

    /* Begin multislice loop ******************************/       
    msloop(seqcon[1],ns,vms_slices,vms_ctr);

      if (!trtype) delay(tr_delay);         // Relaxation delay

      if (ticks > 0) {
        modn(vms_ctr,vtrigblock,vtest);
        ifzero(vtest);                      // if the beginning of an trigger block
          xgate(ticks);
          grad_advance(gpropdelay);
          delay(4e-6);
        elsenz(vtest);
          delay(4e-6);
        endif(vtest);
      }

      sp1on(); delay(4e-6); sp1off();       // Scope trigger

      /* Prepulse options ***********************************/
      if (ir[0] == 'y')   inversion_recovery();
      if (sat[0] == 'y')  satbands();
      if (fsat[0] == 'y') fatsat();
      if (mt[0] == 'y')   mtc();

      /* Slice select RF pulse ******************************/ 
      obspower(p1_rf.powerCoarse);
      obspwrf(p1_rf.powerFine);
      delay(4e-6);
      obl_shapedgradient(ss_grad.name,ss_grad.duration,0,0,ss_grad.amp,NOWAIT);
      delay(ss_grad.rfDelayFront);
      shapedpulselist(shapeEx,ss_grad.rfDuration,oph,rof1,rof2,seqcon[1],vms_ctr);
      delay(ss_grad.rfDelayBack);

      /* Phase encode, refocus, and dephase gradient ********/
      if (sepSliceRephase) {                // separate slice refocus gradient
        obl_shapedgradient(ssr_grad.name,ssr_grad.duration,0,0,-ssr_grad.amp,WAIT);
        delay(te_delay);                    // delay between slab refocus and pe
        pe_shapedgradient(pe_grad.name,pe_grad.duration,-ror_grad.amp,0,0,
            -pe_grad.increment,vpe_mult,WAIT);
      } else {
        pe_shapedgradient(pe_grad.name,pe_grad.duration,-ror_grad.amp,0,-ssr_grad.amp,
            -pe_grad.increment,vpe_mult,WAIT);
        delay(te_delay);                    // delay after refocus/pe
      }

      /* Readout gradient and acquisition *******************/
      if (slprofile[0] == 'y')
        obl_shapedgradient(ro_grad.name,ro_grad.duration,0,0,ro_grad.amp,NOWAIT);
      else
        obl_shapedgradient(ro_grad.name,ro_grad.duration,ro_grad.amp,0,0,NOWAIT);
      delay(ro_grad.atDelayFront);
      startacq(alfa);
      acquire(np,1.0/sw);
      delay(ro_grad.atDelayBack);
      endacq();

      /* Rewind / spoiler gradient **************************/
      if ((perewind[0] == 'y') || (spoilflag[0] == 'y')) {
        pe_shapedgradient(pe_grad.name,pe_grad.duration,spoil_grad.amp,0,0,
            pe_grad.increment,vper_mult,WAIT);
      }

    endmsloop(seqcon[1],vms_ctr);

    /* Optional output control examples *******************/       
    if (mriout[0]=='b' || mriout[1]=='b')
      writeMRIUserByte(vpe_ctr);
    if (mriout[0]=='g' || mriout[1]=='g')
      setMRIUserGates(vpe_ctr);

  endpeloop(seqcon[2],vpe_ctr);

  /* Inter-image delay **********************************/
  sub(ntrt,ct,vtrimage);
  decr(vtrimage);
  ifzero(vtrimage);
    delay(trimage);
  endif(vtrimage);

  calc_grad_duty(tr);

}
