/* MC-HYBRID.C - Procedure for performing Hybrid Monte Carlo updates. */

/* Copyright (c) 1995-2019 by Radford M. Neal 
 *
 * Permission is granted for anyone to copy, use, modify, or distribute this
 * program and accompanying programs and documents for any purpose, provided 
 * this copyright notice is retained and prominently displayed, along with
 * a note saying that the original programs are available from Radford Neal's
 * web page, and note is made of any changes made to the programs.  The
 * programs and documents are distributed without any warranty, express or
 * implied.  As the programs were written for research purposes only, they have
 * not been tested to the degree that would be advisable in any important
 * application.  All use of these programs is entirely at the user's own risk.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "misc.h"
#include "rand.h"
#include "log.h"
#include "mc.h"
  

/* PERFORM HYBRID MONTE CARLO OPERATION - FIRST FORM (MAYBE HAS WINDOWS). */

void mc_hybrid
( mc_dynamic_state *ds,	/* State to update */
  mc_iter *it,		/* Description of this iteration */
  int firsti,		/* Index of first component to update (-1 for all) */
  int lasti,		/* Index of last component to update */
  mc_traj *tj,		/* Trajectory specification */
  int steps,		/* Total number of steps to do */
  int window,		/* Window size to use */
  int jump,		/* Number of steps in each jump */
  double temper_factor,	/* Temper factor, zero if not tempering */
  int N_quantities,	/* Number of quantities to plot, -1 for tt plot */
  mc_value *q_save,	/* Place to save original q */
  mc_value *p_save,	/* Place to save original p */
  mc_value *q_asv,	/* Place to save q values for accept state */
  mc_value *p_asv,	/* Place to save p values for accept state */
  mc_value *q_rsv,	/* Place to save q values for reject state */
  mc_value *p_rsv	/* Place to save p values for reject state */
)
{ 
  double rej_pot_energy, rej_kinetic_energy, rej_free_energy;
  double acc_pot_energy, acc_kinetic_energy, acc_free_energy;
  double old_pot_energy, old_kinetic_energy;

  int have_rej, rej_point;
  int have_acc, acc_point;

  int n, k, dir, jmps;
  double a, H, U;

  if (steps%jump!=0) abort();

  mc_set_range (ds, &firsti, &lasti, 0);

  jmps = steps/jump;

  /* Decide on window offset, and save start state if we'll have to restore. */

  it->window_offset = rand_int(window);

  if (it->window_offset>0)  
  {
    mc_value_copy (q_save, ds->q, ds->dim);
    mc_value_copy (p_save, ds->p, ds->dim);

    if (!ds->know_pot)
    { mc_app_energy (ds, 1, 1, &ds->pot_energy, 0);
      ds->know_pot = 1;
    }
  
    if (!ds->know_kinetic)
    { ds->kinetic_energy = mc_kinetic_energy(ds);
      ds->know_kinetic = 1;
    }

    old_pot_energy = ds->pot_energy;
    old_kinetic_energy = ds->kinetic_energy;
  }

  /* Set up for travelling about trajectory. */

  mc_traj_init(tj,it,firsti,lasti);
  mc_traj_permute();

  have_rej = have_acc = 0;
  n = it->window_offset;
  dir = -1;

  /* Loop that looks at one new state each iteration, which may possibly
     be in the accept and/or reject windows. */

  if (temper_factor!=0 && N_quantities<0)
  { printf("\n");
  }

  while (dir==-1 || n!=jmps)
  {
    /* Restore if next state should be original start state. */

    if (dir==-1 && n==0)
    { 
      if (it->window_offset>0)
      { 
        mc_value_copy (ds->q, q_save, ds->dim);
        mc_value_copy (ds->p, p_save, ds->dim);
 
        ds->pot_energy = old_pot_energy;
        ds->kinetic_energy = old_kinetic_energy;

        ds->know_pot     = 1;
        ds->know_kinetic = 1;
        ds->know_grad    = 0;
      }

      n = it->window_offset;
      dir = 1;
    }

    else 
    { 
      /* Do tempering in second half if appropriate. */

      if (temper_factor!=0 && n<=jmps-window && 2*n>jmps)
      { for (k = 0; k<ds->dim; k++)
        { ds->p[k] /= temper_factor;
        } 
        if (N_quantities<0)
        { printf ("%6d %20.10f\n", jmps-n+1, 
                   mc_kinetic_energy(ds) * (temper_factor*temper_factor - 1));
          printf ("%6d %20.10f\n", jmps-n, 
                   mc_kinetic_energy(ds) * (temper_factor*temper_factor - 1));
        }
      }

      /* Next state is found by following trajectory for 'jump' steps. */

      n += dir;
      mc_trajectory (ds, dir * jump, n<window || n>jmps-window);

      /* Do tempering in first half if appropriate. */

      if (temper_factor!=0 && n>=window && 2*n<jmps)
      { 
        if (N_quantities<0)
        { printf ("%6d %20.10f\n", n, 
                   mc_kinetic_energy(ds) * (temper_factor*temper_factor - 1));
          printf ("%6d %20.10f\n", n+1, 
                   mc_kinetic_energy(ds) * (temper_factor*temper_factor - 1));
          if (2*(n+1)>=jmps) printf("\n");
        }
        for (k = 0; k<ds->dim; k++)
        { ds->p[k] *= temper_factor;
        } 
      }
    }

    /* Make sure we know energy, if needed. */

    if (n<window || n>jmps-window)
    {
      if (!ds->know_pot)
      { mc_app_energy (ds, 1, 1, &ds->pot_energy, 0);
        ds->know_pot = 1;
      }
   
      if (!ds->know_kinetic)
      { ds->kinetic_energy = mc_kinetic_energy(ds);
        ds->know_kinetic = 1;
      }

      H = ds->pot_energy + ds->kinetic_energy;
    }

    /* Account for state in reject window.  Reject window can be ignored if
       windows consist of the entire trajectory. */
  
    if (window!=jmps+1 && n<window)
    { 
      rej_free_energy = !have_rej ? H/it->temperature : 
                          - addlogs (-rej_free_energy, -H/it->temperature);

      if (!have_rej || rand_uniform() < exp(rej_free_energy-H/it->temperature))
      { 
        mc_value_copy (q_rsv, ds->q, ds->dim);
        mc_value_copy (p_rsv, ds->p, ds->dim);

        rej_pot_energy = ds->pot_energy;
        rej_kinetic_energy = ds->kinetic_energy;

        rej_point = n;
        have_rej = 1;
      }
    }

    /* Account for state in the accept window. */

    if (n>jmps-window)
    {
      acc_free_energy = !have_acc ? H/it->temperature : 
                          - addlogs (-acc_free_energy, -H/it->temperature);

      if (!have_acc || rand_uniform() < exp(acc_free_energy-H/it->temperature))
      { 
        if (n!=jmps)
        { mc_value_copy (q_asv, ds->q, ds->dim);
          mc_value_copy (p_asv, ds->p, ds->dim);
        }
  
        acc_pot_energy = ds->pot_energy;
        acc_kinetic_energy = ds->kinetic_energy;
  
        acc_point = n;
        have_acc = 1;
      }
    }

  }

  /* Take new state from the appropriate window. */

  if (!have_acc || !have_rej) abort();

  it->proposals += 1;
  it->delta = (acc_free_energy - rej_free_energy) * it->temperature;

  U = mc_slevel(ds);
  a = exp(-it->delta/it->temperature);

  if (U<a)
  { 
    it->move_point = jump * (acc_point - it->window_offset);
    if (it->consecutive_accepts<=0)
    { it->consecutive_accepts = 1;
    }
    else
    { it->consecutive_accepts += 1;
    }

    if (acc_point!=jmps)
    { mc_value_copy (ds->q, q_asv, ds->dim);
      mc_value_copy (ds->p, p_asv, ds->dim);
      ds->pot_energy     = acc_pot_energy;
      ds->kinetic_energy = acc_kinetic_energy;
      ds->know_pot     = 1;
      ds->know_kinetic = 1;
      ds->know_grad    = 0;
    }

    for (k = 0; k<ds->dim; k++)
    { ds->p[k] = -ds->p[k];
    }

    ds->slevel.value /= a;
  }
  else
  { 
    it->rejects += 1;
    if (it->consecutive_accepts<=0)
    { it->consecutive_accepts = 0;
    }
    else
    { it->consecutive_accepts = -it->consecutive_accepts;
    }
    it->move_point = jump * (rej_point - it->window_offset);

    mc_value_copy (ds->q, q_rsv, ds->dim);
    mc_value_copy (ds->p, p_rsv, ds->dim);

    ds->pot_energy     = rej_pot_energy;
    ds->kinetic_energy = rej_kinetic_energy;
    ds->know_pot     = 1;
    ds->know_kinetic = 1;
    ds->know_grad    = 0;
  }
}
  

/* PERFORM HYBRID MONTE CARLO OPERATION - SECOND FORM (ACCEPTS BY THRESHOLD). */

void mc_hybrid2
( mc_dynamic_state *ds,	/* State to update */
  mc_iter *it,		/* Description of this iteration */
  int firsti,		/* Index of first component to update (-1 for all) */
  int lasti,		/* Index of last component to update */
  mc_traj *tj,		/* Trajectory specification */
  int steps,		/* Maximum number of steps to do */
  int in_steps,		/* Maximum number of acceptable steps to do */
  int jump,		/* Number of steps in each jump */
  mc_value *q_save,	/* Place to save original q */
  mc_value *p_save	/* Place to save original p */
)
{
  double old_pot_energy, old_kinetic_energy;
  double threshold, H;
  int n, in, k;

  if (steps%jump!=0 || in_steps%jump!=0) abort();

  mc_set_range (ds, &firsti, &lasti, 0);

  if (!ds->know_pot)
  { mc_app_energy (ds, 1, 1, &ds->pot_energy, ds->grad);
    ds->know_pot = 1;
    ds->know_grad = 1;
  }

  if (!ds->know_kinetic)
  { ds->kinetic_energy = mc_kinetic_energy(ds);
    ds->know_kinetic = 1;
  }

  mc_value_copy (q_save, ds->q, ds->dim);
  mc_value_copy (p_save, ds->p, ds->dim);

  old_pot_energy = ds->pot_energy;
  old_kinetic_energy = ds->kinetic_energy;

  threshold = old_pot_energy + old_kinetic_energy 
               + it->temperature * mc_slice_inc(ds);

  mc_traj_init(tj,it,firsti,lasti);
  mc_traj_permute();

  n = 0;
  in = 0;

  while (n<steps && in<in_steps)
  {
    mc_trajectory (ds, jump, 1);

    if (!ds->know_pot)
    { mc_app_energy (ds, 1, 1, &ds->pot_energy, ds->grad);
      ds->know_pot = 1;
      ds->know_grad = 1;
    }

    if (!ds->know_kinetic)
    { ds->kinetic_energy = mc_kinetic_energy(ds);
      ds->know_kinetic = 1;
    }

    H = ds->pot_energy + ds->kinetic_energy;

    n += jump;
    if (H<=threshold) in += 1;
  }

  it->proposals += 1;
  it->delta = H - old_pot_energy - old_kinetic_energy;

  if (H<=threshold)
  { 
    it->move_point = n;
    if (it->consecutive_accepts<=0)
    { it->consecutive_accepts = 1;
    }
    else
    { it->consecutive_accepts += 1;
    }

    for (k = 0; k<ds->dim; k++)
    { ds->p[k] = -ds->p[k];
    }

    ds->slevel.value /= exp(-it->delta/it->temperature);
  }
  else
  { 
    it->rejects += 1;
    if (it->consecutive_accepts<=0)
    { it->consecutive_accepts = 0;
    }
    else
    { it->consecutive_accepts = -it->consecutive_accepts;
    }
    it->move_point = 0;

    mc_value_copy (ds->q, q_save, ds->dim);
    mc_value_copy (ds->p, p_save, ds->dim);

    ds->pot_energy     = old_pot_energy;
    ds->kinetic_energy = old_kinetic_energy;
    ds->know_pot     = 1;
    ds->know_kinetic = 1;
    ds->know_grad    = 0;
  }
}
  

/* PERFORM SPIRAL DYNAMICS OPERATION. */

#define Spiral_debug 0
 
void mc_spiral
( mc_dynamic_state *ds,	/* State to update */
  mc_iter *it,		/* Description of this iteration */
  mc_traj *tj,		/* Trajectory specification */
  int steps,		/* Total number of steps to do */
  double temper_factor,	/* Temper factor */
  int dbl,		/* Use a double spiral? */
  mc_value *q_save,	/* Place to save original q */
  mc_value *p_save,	/* Place to save original p */
  mc_value *q_asv,	/* Place to save q values for accept state */
  mc_value *p_asv	/* Place to save p values for accept state */
)
{ 
  double acc_pot_energy, acc_kinetic_energy, acc_free_energy;
  double old_pot_energy, old_kinetic_energy;
  int offset, switch_point, acc_point;
  int n, k, dir;
  double A, sqtf, lgf;

  sqtf = sqrt(temper_factor);
  lgf  = log (temper_factor);

  /* Choose the offset of the start state, and the point to switch to
     a contracting spiral. */

  offset = rand_int(steps+1);
  switch_point = dbl ? rand_int(steps+1) : steps;

  /* Save the start state, also make it the accept state at the beginning. */

  mc_value_copy (q_save, ds->q, ds->dim);
  mc_value_copy (q_asv, ds->q, ds->dim);

  mc_value_copy (p_save, ds->p, ds->dim);
  mc_value_copy (p_asv, ds->p, ds->dim);

  if (!ds->know_pot)
  { mc_app_energy (ds, 1, 1, &ds->pot_energy, 0);
    ds->know_pot = 1;
  }
  
  if (!ds->know_kinetic)
  { ds->kinetic_energy = mc_kinetic_energy(ds);
    ds->know_kinetic = 1;
  }

  old_pot_energy = ds->pot_energy;
  old_kinetic_energy = ds->kinetic_energy;

  acc_pot_energy = ds->pot_energy;
  acc_kinetic_energy = ds->kinetic_energy;

  acc_free_energy = ds->pot_energy + ds->kinetic_energy 
                      + fabs(offset-switch_point)*lgf*ds->dim;

  acc_point = offset;

  /* Set up for travelling about trajectory. */

  mc_traj_init(tj,it,0,ds->dim-1);
  mc_traj_permute();

  n = offset;
  dir = -1;

  if (Spiral_debug)
  { printf(" n dir q p E K F T pr\n");
    printf("%4d %+d %+10.3e %+6.3f %+6.2f %+6.2f %+6.2f %+6.2f %.3f\n",
      n, dir, ds->q[0], ds->p[0],
      ds->pot_energy, ds->kinetic_energy, fabs(n-switch_point)*lgf*ds->dim, 
      ds->pot_energy + ds->kinetic_energy + fabs(n-switch_point)*lgf*ds->dim,
      1.0);
  }

  /* Loop that looks at one new state each iteration. */

  for (;;)
  {
    /* Restore if next state should be a step from the original start state. */

    if (dir==-1 && n==0)
    { 
      mc_value_copy (ds->q, q_save, ds->dim);
      mc_value_copy (ds->p, p_save, ds->dim);
 
      ds->pot_energy = old_pot_energy;
      ds->kinetic_energy = old_kinetic_energy;

      ds->know_pot     = 1;
      ds->know_kinetic = 1;
      ds->know_grad    = 0;

      n = offset;
      dir = 1;
    }

    /* See if we're done. */

    if (dir==1 && n==steps) break;

    /* Multiply/divide, do trajectory step, and multiply/divide again. */

    if (dir==1 ? n>=switch_point : n<switch_point)
    { for (k = 0; k<ds->dim; k++)
      { ds->p[k] /= sqtf;
      } 
    }
    else
    { for (k = 0; k<ds->dim; k++)
      { ds->p[k] *= sqtf;
      } 
    }

    mc_trajectory (ds, dir, 1);

    if (dir==1 ? n>=switch_point : n<switch_point)
    { for (k = 0; k<ds->dim; k++)
      { ds->p[k] /= sqtf;
      } 
    }
    else
    { for (k = 0; k<ds->dim; k++)
      { ds->p[k] *= sqtf;
      } 
    }

    /* Make sure we know energy. */

    if (!ds->know_pot)
    { mc_app_energy (ds, 1, 1, &ds->pot_energy, 0);
      ds->know_pot = 1;
    }
   
    ds->kinetic_energy = mc_kinetic_energy(ds);
    ds->know_kinetic = 1;

    /* Advance to next position. */

    n += dir;

    /* Update accepted point and accept free energy accounting for this state.*/

    A = ds->pot_energy + ds->kinetic_energy + fabs(n-switch_point)*lgf*ds->dim;

    acc_free_energy = - addlogs (-acc_free_energy, -A);

    if (Spiral_debug)
    { printf("%4d %+d %+10.3e %+6.3f %+6.2f %+6.2f %+6.2f %+6.2f %.3f\n",
        n, dir, ds->q[0], ds->p[0],
        ds->pot_energy, ds->kinetic_energy, fabs(n-switch_point)*lgf*ds->dim, 
        A, exp(acc_free_energy-A));
    }

    if (rand_uniform() < exp(acc_free_energy-A))
    { 
      mc_value_copy (q_asv, ds->q, ds->dim);
      mc_value_copy (p_asv, ds->p, ds->dim);
  
      acc_pot_energy = ds->pot_energy;
      acc_kinetic_energy = ds->kinetic_energy;
  
      acc_point = n;
    }
  }

  /* Go to the point that was accepted in the end. */

  it->move_point = acc_point - offset;
  it->spiral_offset = offset;
  it->spiral_switch = switch_point;

  mc_value_copy (ds->q, q_asv, ds->dim);
  mc_value_copy (ds->p, p_asv, ds->dim);

  ds->pot_energy     = acc_pot_energy;
  ds->kinetic_energy = acc_kinetic_energy;
  ds->know_pot     = 1;
  ds->know_kinetic = 1;
  ds->know_grad    = 0;
}
