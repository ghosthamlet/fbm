/* MC-ITER.C - Procedures for performing Markov chain Monte Carlo iterations. */

/* Copyright (c) 1995 by Radford M. Neal 
 *
 * Permission is granted for anyone to copy, use, or modify this program 
 * for purposes of research or education, provided this copyright notice 
 * is retained, and note is made of any changes that have been made. 
 *
 * This program is distributed without any warranty, express or implied.
 * As this program was written for research purposes only, it has not been
 * tested to the degree that would be advisable in any important application.
 * All use of this program is entirely at the user's own risk.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "misc.h"
#include "rand.h"
#include "log.h"
#include "mc.h"
#include "quantities.h"


#define Print_proposals 0	/* Print proposed changes by quadrant? 
				   (not normally done, for test only) */


/* STRUCTURE AND PROCEDURE USED FOR SORTING. */

typedef struct { int index; double value; } sort_table;

int compare (const void *a, const void *b) 
{ 
  return ((sort_table*)a)->value < ((sort_table*)b)->value ? -1
       : ((sort_table*)a)->value > ((sort_table*)b)->value ? +1
       : 0;
}


/* LOCAL VARIABLES. */

#define Max_temp_repeat	1000 /* Max repetition count in a tempered transition */

static int echo = 0;	/* Debug option - echos operations first time */

static mc_ops *ops;	/* Operations to perform each iteration */
static int endo;               /* Index of point past last operation */
static int endp[2*Max_mc_ops]; /* Other endpoint for group operations */

static mc_traj *tj;	/* Trajectory specification */
static mc_temp_sched *sch; /* Tempering schedule, zero if none */

static int need_p;	/* Do we need momentum? */
static int need_grad;	/* Do we need gradient? */

static int have_ss;	/* Do we have the stepsizes? */

static int need_save;	/* Do we need space to save q and (maybe) p? */
static mc_value *q_save;/* Place to save q */
static mc_value *p_save;/* Place to save p */

static int need_savet;	/* Do we need space to save for tempered transitions? */
static mc_value *q_savet;/* Place to save q for tempered transitions */
static mc_value *p_savet;/* Place to save p for tempered transitions */
static mc_value *aux_savet; /* Place to save auxiliary variables */

static int need_arsv;	/* Do we need space to save accept/reject states? */
static mc_value *q_asv;	/* Place to save q values for accept state */
static mc_value *p_asv;	/* Place to save p values for accept state */
static mc_value *q_rsv;	/* Place to save q values for reject state */
static mc_value *p_rsv;	/* Place to save p values for reject state */

static int print_index;	/* Index used to label printed quantities */

static struct 		/* States saved for over-relaxation */
{ mc_value *p, *q, *aux; 
} osave[Max_temp_repeat];

static sort_table osort[Max_temp_repeat]; /* Sort table for over-relaxation */


/* LOCAL PROCEDURES. */

static void do_group (mc_dynamic_state *, mc_iter *, int, int, int,
                      log_gobbled *, quantities_described, int);

static void metropolis (mc_dynamic_state *, mc_iter *);
static void hybrid (mc_dynamic_state *, mc_iter *, int, int, int, double, int);
static void simulated_tempering (mc_dynamic_state *, mc_iter *);
static void tempered_transition (mc_dynamic_state *, mc_iter *, 
                                 int, int, int, int, int,
                                 log_gobbled *, quantities_described, int);
static void temp_over (mc_dynamic_state *, mc_iter *, int, int, int, 
                       log_gobbled *, quantities_described, int);

static void copy (mc_value *, mc_value *, int);


/* SET UP FOR PERFORMING ITERATIONS. */

void mc_iter_init 
( mc_dynamic_state *ds,	/* State to update */
  mc_ops *ops0,		/* Operations to perform each iteration */
  mc_traj *tj0,		/* Trajectory specification */
  mc_temp_sched *sch0	/* Tempering schedule, zero if none */
)
{  
  int depth, type, i;
  int stack[Max_mc_ops];
  int does_print;

  ops = ops0;
  tj = tj0;
  sch = sch0;

  need_p = need_grad = need_save = need_savet = need_arsv = 0;
  does_print = 0;

  depth = 0;

  for (i = 0; i<Max_mc_ops && ops->op[i].type!=0; i++)
  { 
    type = ops->op[i].type;

    endp[i] = i;

    if (strchr(Group_ops,type)!=0)
    { stack[depth] = i;
      depth += 1;
    }

    if (type=='E')
    { depth -= 1;
      if (depth<0) 
      { fprintf(stderr,"Too many 'end' ops in Monte Carlo operations list\n");
        exit(1);
      }
      endp[stack[depth]] = i;
      endp[i] = stack[depth];
    }

    if (type=='B' || type=='N' || type=='D' || type=='P' 
     || type=='H' || type=='T')
    { need_p = 1;
    }

    if (type=='D' || type=='P' || type=='H' || type=='T')
    { need_grad = 1;
    }

    if (type=='M' || type=='H' || type=='T')
    { need_save = 1;
    }

    if (type=='H' || type=='T')
    { need_arsv = 1;
    }

    if (type=='t')
    { need_savet = 1;
    }

    if (type=='p') 
    { does_print = 1;   
    }
  }

  while (depth>0)
  { depth -= 1;
    endp[stack[depth]] = i;
    endp[i] = stack[depth];
    i += 1;
  }

  endo = i-1;

  if (need_save)
  { q_save = chk_alloc (ds->dim, sizeof *q_save);
    p_save = chk_alloc (ds->dim, sizeof *p_save);
  }

  if (need_savet)
  { q_savet = chk_alloc (ds->dim, sizeof *q_savet);
    p_savet = chk_alloc (ds->dim, sizeof *p_savet);
    if (ds->aux_dim>0) aux_savet = chk_alloc (ds->aux_dim, sizeof *p_savet);
  }

  if (need_arsv)
  { q_asv = chk_alloc (ds->dim, sizeof *q_asv);
    p_asv = chk_alloc (ds->dim, sizeof *p_asv);
    q_rsv = chk_alloc (ds->dim, sizeof *q_rsv);
    p_rsv = chk_alloc (ds->dim, sizeof *p_rsv);
  }
}


/* PERFORM ONE ITERATION.  A pointer to the structure holding data on this
   iteration is passed.  The caller will have set the temperature and decay 
   fields as specified by the user.  The other fields are set by this 
   procedure, or added to, in the case of rejects and proposals. */

void mc_iteration
( mc_dynamic_state *ds,	/* State to update */
  mc_iter *it,		/* Description of this iteration */
  log_gobbled *logg,	/* Records gobbled */
  void *qd, 		/* Descriptions of quantities to plot */
  int N_quantities	/* Number of quantities to plot, -1 for tt plot */
)
{ 
  int i, j, k, n, na;

  /* Create momentum variables and gradient vector if needed. */

  if (need_p && ds->p==0)
  { 
    ds->p = chk_alloc (ds->dim, sizeof (mc_value));
    mc_heatbath (ds, it->temperature, 0.0);
    ds->know_kinetic = 0;
  }

  if (need_grad && ds->grad==0)
  {
    ds->grad = chk_alloc (ds->dim, sizeof (mc_value));
    ds->know_grad = 0;
  }

  /* Initialize fields describing iteration, except those that are additive. */

  it->stepsize_factor = 1.0;
  it->move_point = 0;
  it->delta = 0.0;

  /* Set approximation order, usually superseded by a permute call. */

  na = tj->N_approx>0 ? tj->N_approx : -tj->N_approx;
  if (na>Max_approx) na = Max_approx;

  for (j = 0; j<na; j++) it->approx_order[j] = j+1;

  /* Perform the operations. */

  have_ss = 0;

  if (echo)
  { printf("Ops performed:");
  }
 
  print_index = 0;

  do_group(ds,it,0,endo,0,logg,qd,N_quantities);

  if (echo)
  { printf("\n");
    echo = 0;  
  }
}


/* PERFORM A GROUP OF OPERATIONS. */

static void do_group
( mc_dynamic_state *ds,	/* State to update */
  mc_iter *it,		/* Description of this iteration */
  int start,		/* Index of first operation in group */
  int end,		/* Index of terminator for group of operations */
  int reverse,		/* Do operations in reverse order? */
  log_gobbled *logg,	/* Records gobbled */
  quantities_described qd, /* Descriptions of quantities to plot */
  int N_quantities	/* Number of quantities to plot, -1 for tt plot */
)
{
  double alpha, stepsize_adjust;
  int type;

  int i, k, c;

  if (end<start) return;

  if (reverse)
  { i = endp[end];
  }
  else
  { i = start;
  }

  for (;;)
  {
    if (i>=Max_mc_ops) abort();

    type = ops->op[i].type;

    if (type==0) abort();

    if (echo)
    { printf(" %d%c",i,type);
    }

    /* Figure out what stepsize factor to use this time, if necessary. */

    if (type=='M' || type=='D' || type=='P' || type=='H' || type=='T')
    { 
      stepsize_adjust = ops->op[i].stepsize_adjust;
      alpha = ops->op[i].stepsize_alpha;

      it->stepsize_factor = 
        stepsize_adjust>0 ? stepsize_adjust : -stepsize_adjust;

      if (alpha!=0) 
      { it->stepsize_factor *= 
          alpha>0 ? 1 / sqrt (rand_gamma(alpha/2) / (alpha/2))
                  : pow(10.0,-alpha*(rand_uniopen()-0.5));
          /* Used to be just rand_gamma(alpha) / alpha */
      }

      if (!have_ss)
      {
        if (stepsize_adjust>0)
        { mc_app_stepsizes (ds);
        }
        else
        { for (k = 0; k<ds->dim; k++) ds->stepsize[k] = 1.0;
        }

        have_ss = 1;
      }
    }

    /* Do the next operation. */

    switch (type)
    { 
      case 'E': abort();

      case 'R': 
      { for (c = 0; c<ops->op[i].repeat_count; c++)
        { do_group(ds,it,i+1,endp[i]-1,reverse,logg,qd,N_quantities);
        }
        break;
      }

      case 'B':
      { mc_heatbath (ds, it->temperature, 
                     it->decay>=0 ? it->decay : ops->op[i].heatbath_decay);
        break;
      }

      case 'N':
      { for (k = 0; k<ds->dim; k++) 
        { ds->p[k] = -ds->p[k];
        }
        break;
      }

      case 'M': 
      { metropolis(ds,it);
        break;
      }

      case 'D':
      { mc_traj_init(tj,it);
        mc_trajectory(ds,ops->op[i].steps);
        break;
      }

      case 'P':
      { mc_traj_init(tj,it);
        mc_traj_permute();
        mc_trajectory(ds,ops->op[i].steps);
        break;
      }

      case 'H': case 'T':
      { hybrid (ds, it, ops->op[i].steps, ops->op[i].window, ops->op[i].jump,
                type=='H' ? 0.0 : ops->op[i].temper_factor, N_quantities);
        break;
      }

      case 's':
      { simulated_tempering(ds,it);
        have_ss = 0;
        break;
      }

      case 't':
      { tempered_transition (ds, it, i+1, endp[i]-1, reverse, 
          ops->op[i].repeat_count, ops->op[i].high_count, 
          logg, qd, N_quantities);
        break;
      }

      case 'n':
      { mc_temp_present(ds,sch);
        ds->temp_state->temp_dir = -ds->temp_state->temp_dir;
        break;
      }

      case 'b':
      { mc_temp_present(ds,sch);
        ds->temp_state->temp_dir = rand_int(2) ? -1 : +1;
        break;
      }

      case 'p':
      { quantities_held *qh;
        int v;

        if (N_quantities<=0) break;

        if (N_quantities==1) 
        { if (print_index==0) printf("\n");
          printf("%6d",print_index);
        }

        qh = quantities_storage(qd);
        quantities_evaluate(qd,qh,logg);
        for (v = 0; v<N_quantities; v++) 
        { printf(" %20.8le",*qh->value[v]);
        }
        printf("\n");
        print_index += 1;
        free(qh);

        break;
      }

      case 'A':
      { if (!mc_app_sample (ds, ops->op[i].appl, ops->op[i].app_param, it))
        { fprintf(stderr,"Unknown application-specific operation: %s\n",
            ops->op[i].appl);
          exit(1);
        }
        have_ss = 0;
        break;
      }

      default:
      { fprintf(stderr,"Unknown operation type encountered: %c\n", type);
        exit(1);
      }
    }

    if (reverse && i==start || !reverse && endp[i]==end) break;

    if (reverse)
    { i = endp[i-1];
    }
    else
    { i = endp[i]+1;
    }
      
  }
}


/* PERFORM METROPOLIS UPDATE. */

static void metropolis
( mc_dynamic_state *ds,	/* State to update */
  mc_iter *it		/* Description of this iteration */
)
{
  double old_energy;
  double sf;
  int k;

  if (!ds->know_pot)
  { mc_app_energy (ds, 1, 1, &ds->pot_energy, 0);
    ds->know_pot = 1;
  }

  old_energy = ds->pot_energy;

  sf = it->stepsize_factor;

  copy (q_save, ds->q, ds->dim);

  for (k = 0; k<ds->dim; k++) 
  { ds->q[k] += sf * ds->stepsize[k] * rand_gaussian();
  }
  
  mc_app_energy (ds, 1, 1, &ds->pot_energy, 0);

  it->proposals += 1;
  it->delta = ds->pot_energy - old_energy;

  if (it->delta<=0 || rand_uniform() < exp(-it->delta/it->temperature))
  { 
    it->move_point = 1;
    ds->know_grad = 0;
  }
  else
  { 
    it->rejects += 1;
    it->move_point = 0;

    ds->pot_energy = old_energy;

    copy (ds->q, q_save, ds->dim);
  }
}


/* PERFORM HYBRID MONTE CARLO OPERATION. */

static void hybrid
( mc_dynamic_state *ds,	/* State to update */
  mc_iter *it,		/* Description of this iteration */
  int steps,		/* Total number of steps to do */
  int window,		/* Window size to use */
  int jump,		/* Number of steps in each jump */
  double temper_factor,	/* Temper factor, zero if not tempering */
  int N_quantities	/* Number of quantities to plot, -1 for tt plot */
)
{ 
  double rej_pot_energy, rej_kinetic_energy, rej_free_energy;
  double acc_pot_energy, acc_kinetic_energy, acc_free_energy;
  double old_pot_energy, old_kinetic_energy;

  int have_rej, rej_point;
  int have_acc, acc_point;

  int n, k, dir, jmps;
  double H;

  if (steps%jump!=0) abort();

  jmps = steps/jump;

  /* Decide on window offset, and save start state if we'll have to restore. */

  it->window_offset = rand_int(window);

  if (it->window_offset>0)  
  {
    copy (q_save, ds->q, ds->dim);
    copy (p_save, ds->p, ds->dim);

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

  mc_traj_init(tj,it);
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
        copy (ds->q, q_save, ds->dim);
        copy (ds->p, p_save, ds->dim);
 
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
        { printf ("%6d %20.10lf\n", jmps-n+1, 
                   mc_kinetic_energy(ds) * (temper_factor*temper_factor - 1));
          printf ("%6d %20.10lf\n", jmps-n, 
                   mc_kinetic_energy(ds) * (temper_factor*temper_factor - 1));
        }
      }

      /* Next state is found by following trajectory for 'jump' steps. */

      mc_trajectory (ds, dir * jump);
      n += dir;

      /* Do tempering in first half if appropriate. */

      if (temper_factor!=0 && n>=window && 2*n<jmps)
      { 
        if (N_quantities<0)
        { printf ("%6d %20.10lf\n", n, 
                   mc_kinetic_energy(ds) * (temper_factor*temper_factor - 1));
          printf ("%6d %20.10lf\n", n+1, 
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
      rej_free_energy = !have_rej ? H : - addlogs (-rej_free_energy, -H);

      if (!have_rej || rand_uniform() < exp(rej_free_energy-H))
      { 
        copy (q_rsv, ds->q, ds->dim);
        copy (p_rsv, ds->p, ds->dim);

        rej_pot_energy = ds->pot_energy;
        rej_kinetic_energy = ds->kinetic_energy;

        rej_point = n;
        have_rej = 1;
      }
    }

    /* Account for state in the accept window. */

    if (n>jmps-window)
    {
      acc_free_energy = !have_acc ? H : - addlogs (-acc_free_energy, -H);

      if (!have_acc || rand_uniform() < exp(acc_free_energy-H))
      { 
        if (n!=jmps)
        { copy (q_asv, ds->q, ds->dim);
          copy (p_asv, ds->p, ds->dim);
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
  it->delta = acc_free_energy - rej_free_energy;

  if (it->delta<=0 || rand_uniform() < exp(-it->delta/it->temperature))
  { 
    it->move_point = jump * (acc_point - it->window_offset);

    if (acc_point!=jmps)
    { copy (ds->q, q_asv, ds->dim);
      copy (ds->p, p_asv, ds->dim);
      ds->pot_energy     = acc_pot_energy;
      ds->kinetic_energy = acc_kinetic_energy;
      ds->know_pot     = 1;
      ds->know_kinetic = 1;
      ds->know_grad    = 0;
    }

    for (k = 0; k<ds->dim; k++)
    { ds->p[k] = -ds->p[k];
    }
  }
  else
  { 
    it->rejects += 1;
    it->move_point = jump * (rej_point - it->window_offset);

    copy (ds->q, q_rsv, ds->dim);
    copy (ds->p, p_rsv, ds->dim);

    ds->pot_energy     = rej_pot_energy;
    ds->kinetic_energy = rej_kinetic_energy;
    ds->know_pot     = 1;
    ds->know_kinetic = 1;
    ds->know_grad    = 0;
  }
}


/* COPY STATE VARIABLES. */

static void copy
( mc_value *dest,	/* Place to copy to */
  mc_value *src,	/* Place to copy from */
  int n			/* Number of values to copy */
)
{
  while (n>0)
  { *dest++ = *src++;
    n -= 1;
  }
}


/* PERFORM SIMULATED TEMPERING UPDATE.  Does a Metropolis update for a
   proposal to increase or decrease the inverse temperature. */

static void simulated_tempering
( mc_dynamic_state *ds,	/* State to update */
  mc_iter *it		/* Description of this iteration */
)
{
  double old_energy, olde, newe;
  int i;

  if (sch==0)
  { fprintf(stderr,"No tempering schedule has been specified\n");
    exit(1);
  }

  mc_temp_present(ds,sch);

  if (ds->temp_index<0 || ds->temp_index>Max_temps
   || ds->temp_state->temp_dir!=-1 && ds->temp_state->temp_dir!=+1)
  { abort();
  }

  if (!ds->know_pot)
  { mc_app_energy (ds, 1, 1, &ds->pot_energy, 0);
    ds->know_pot = 1;
  }

  old_energy = ds->pot_energy;
  olde = old_energy + sch->sched[ds->temp_index].bias;

  if (ds->temp_index==0 && ds->temp_state->temp_dir==-1 
   || ds->temp_state->inv_temp==1 && ds->temp_state->temp_dir==+1)
  { it->proposals += 1;
    it->delta = 1e30;
    it->move_point = 0;
    it->rejects += 1;
    return;
  }

  ds->temp_index += ds->temp_state->temp_dir;
  ds->temp_state->inv_temp = sch->sched[ds->temp_index].inv_temp;
  
  mc_app_energy (ds, 1, 1, &ds->pot_energy, 0);

  newe = ds->pot_energy + sch->sched[ds->temp_index].bias;

  it->proposals += 1;
  it->delta = newe - olde;

  if (it->delta<=0 || rand_uniform() < exp(-it->delta/it->temperature))
  { 
    it->move_point = 1;
    ds->temp_state->temp_dir = -ds->temp_state->temp_dir;
    ds->know_grad = 0;
  }
  else
  { 
    it->move_point = 0;
    it->rejects += 1;
    ds->pot_energy = old_energy;
    ds->temp_index -= ds->temp_state->temp_dir;
    ds->temp_state->inv_temp = sch->sched[ds->temp_index].inv_temp;
  }
  
}


/* DO A TEMPERED TRANSITION. */

static void tempered_transition
( mc_dynamic_state *ds,	/* State to update */
  mc_iter *it,		/* Description of this iteration */
  int start,		/* Index of first operation in group */
  int end,		/* Index of terminator for group of operations */
  int reverse,		/* Do operations in reverse order? */
  int repeat,		/* Number of times to repeat group */
  int high_count,	/* Special repeat count for highest temperature */
  log_gobbled *logg,	/* Records gobbled */
  quantities_described qd, /* Descriptions of quantities to plot */
  int N_quantities	/* Number of quantities to plot, -1 for tt plot */
)
{
  double down[Max_temps];
  mc_temp_state ts;
  double delta, ed, b1, b2;
  int quad0, quad1;
  int i1, i2, c;

  if (ds->temp_state)
  { fprintf(stderr, "Tempering operations can't be nested\n");
    exit(1);
  }
  ds->temp_state = &ts;

  if (sch==0)
  { fprintf(stderr,"No tempering schedule has been specified\n");
    exit(1);
  }

  if (Print_proposals)
  { quad0 = ds->q[0]>0 && ds->q[1]>0 ? 1
          : ds->q[0]<=0 && ds->q[1]>0 ? 2
          : ds->q[0]<=0 && ds->q[1]<=0 ? 3
          : 4;
  }

  if (logg->data['b']!=0) abort();
  logg->data['b'] = ds->temp_state;
  logg->index['b'] = logg->last_index;

  ds->temp_state->inv_temp = 1.0;
  ds->temp_index = mc_temp_index(sch,1.0);
  ds->temp_state->temp_dir = -1;

  if (N_quantities<0)
  { printf("\n");
  }

  copy (q_savet, ds->q, ds->dim);
  if (ds->p) copy(p_savet, ds->p, ds->dim);
  if (ds->aux) copy(aux_savet, ds->aux, ds->aux_dim);

  delta = 0;

  for (;;)
  { 
    b1 = ds->temp_state->inv_temp;
    i1 = ds->temp_index;

    if (ds->temp_state->temp_dir==-1)
    {
      if (ds->temp_index==0)
      { ds->temp_state->temp_dir = +1;
      }
      else
      { ed = mc_energy_diff(ds,sch,ds->temp_state->temp_dir);
        delta += ed;
        ds->temp_index -= 1;
        ds->know_pot = 0;
        ds->know_grad = 0;
        have_ss = 0;
      }
    }
    else
    { 
      ed = mc_energy_diff(ds,sch,ds->temp_state->temp_dir);
      delta += ed;
      ds->temp_index += 1;
      ds->know_pot = 0;
      ds->know_grad = 0;
      have_ss = 0;
    }

    b2 = ds->temp_state->inv_temp = sch->sched[ds->temp_index].inv_temp;
    i2 = ds->temp_index;

    if (N_quantities==-1) 
    { if (b1==b2)
      { printf("\n");
      }
      else 
      { printf(" %12.10lf %20.10le\n", b1, ed/(b2-b1));
        printf(" %12.10lf %20.10le\n", b2, ed/(b2-b1));
      }
    }

    if (N_quantities==-2)
    { if (ds->temp_state->temp_dir==-1)
      { down[i1] = ed/(b2-b1);
      }
      else
      { printf(" %12.10lf %20.10le\n", b1, ed/(b2-b1) - down[i2]);
        printf(" %12.10lf %20.10le\n", b2, ed/(b2-b1) - down[i2]);
      }
    }
    
    if (ds->temp_state->inv_temp==1.0) 
    { break;  
    }

    if (ds->temp_index==0)
    { for (c = 0; c<high_count; c++)
      { do_group (ds, it, start, end, 
                  (ds->temp_state->temp_dir==-1 ? reverse : !reverse), 
                  logg, qd, (N_quantities<0 ? 0 : N_quantities));
      }
    }
    else if (repeat==1)
    { do_group (ds, it, start, end, 
                (ds->temp_state->temp_dir==-1 ? reverse : !reverse), 
                logg, qd, (N_quantities<0 ? 0 : N_quantities));
    }
    else
    { temp_over(ds, it, repeat, start, end, logg, qd, N_quantities);
    }
  }

  if (Print_proposals)
  { quad1 = ds->q[0]>0 && ds->q[1]>0 ? 1
          : ds->q[0]<=0 && ds->q[1]>0 ? 2
          : ds->q[0]<=0 && ds->q[1]<=0 ? 3
          : 4;
  }

  it->proposals += 1;
  it->delta = delta;

  if (delta<=0 || rand_uniform() < exp(-delta/it->temperature))
  { 
    it->move_point = 1;
  }
  else
  { 
    it->rejects += 1;
    it->move_point = 0;

    copy (ds->q, q_savet, ds->dim);
    if (ds->p) copy(ds->p, p_savet, ds->dim);
    if (ds->aux) copy(ds->aux, aux_savet, ds->aux_dim);
  }

  if (Print_proposals)
  { printf("%d %d %d %.1lf\n",quad0,quad1,it->move_point,it->delta);
    fflush(stdout);
  }

  logg->data['b'] = 0;
  logg->index['b'] = 0;

  ds->temp_state = 0;
  ds->know_pot = 0;
  ds->know_grad = 0;
  have_ss = 0;
}


/* DO AN OVER-RELAXED TEMPERING STEP. */

static void temp_over
( mc_dynamic_state *ds,	/* State to update */
  mc_iter *it,		/* Description of this iteration */
  int repeat,		/* Repetition count */
  int start,		/* Index of first operation in group */
  int end,		/* Index of terminator for group of operations */
  log_gobbled *logg,	/* Records gobbled */
  quantities_described qd, /* Descriptions of quantities to plot */
  int N_quantities	/* Number of quantities to plot, -1 for tt plot */
)
{
  double b1, b2;
  int st, above;
  int i, d;

  b1 = sch->sched[ds->temp_index+1].inv_temp;
  b2 = sch->sched[ds->temp_index].inv_temp;

  if (repeat>Max_temp_repeat)
  { fprintf(stderr,"Repetition count too big in tempered transition (max %d)\n",
                    Max_temp_repeat);
    exit(1);
  }

  for (i = 0; i<repeat; i++)
  { if (osave[i].q==0) 
    { osave[i].q = chk_alloc (ds->dim, sizeof(mc_value));
    }
    if (ds->p!=0 && osave[i].p==0) 
    { osave[i].q = chk_alloc (ds->dim, sizeof(mc_value));
    }
    if (ds->aux!=0 && osave[i].aux==0)
    { osave[i].aux = chk_alloc (ds->aux_dim, sizeof(mc_value));
    }
  }

  above = 0;
  st = rand_int(repeat);

  i = st;
  d = +1;

  if (echo) printf(" <");

  for (;;)
  {
    copy (osave[i].q, ds->q, ds->dim);
    if (ds->p) copy(osave[i].p, ds->p, ds->dim);
    if (ds->aux) copy(osave[i].aux, ds->aux, ds->aux_dim);

    osort[i].index = i;
    osort[i].value = mc_energy_diff (ds, sch, +1);

    if (osort[i].value>osort[st].value) above += 1;

    if (N_quantities==-1)    
    { printf("move %8.6lf %20.10le\n",ds->temp_state->inv_temp, 
                                      osort[i].value/(b1-b2));
      printf("  %8.6lf %20.10le\n",   ds->temp_state->inv_temp, 
                                      osort[i].value/(b1-b2));
    }

    i += d;
    if (i==repeat) 
    { copy (ds->q, osave[st].q, ds->dim);
      if (ds->p) copy(ds->p, osave[st].p, ds->dim);
      if (ds->aux) copy(ds->aux, osave[st].aux, ds->aux_dim);
      ds->know_pot = 0;
      ds->know_grad = 0;
      i = st-1;
      d = -1;
      if (echo) printf(" |");
    }
    if (i<0) break;

    do_group (ds, it, start, end, (d==-1 ? 1 : 0),
              logg, qd, (N_quantities<0 ? 0 : N_quantities));
  }

  if (echo) printf(" >");
  
  qsort (osort, repeat, sizeof *osort, compare);

# if 0
    for (i = 0; i<repeat; i++) 
    { printf(" %2d %20.8le\n",osort[i].index,osort[i].value);
    }
    printf(" - %d -\n",above);
# endif

  i = osort[above].index;

  copy (ds->q, osave[i].q, ds->dim);
  if (ds->p) copy(ds->p, osave[i].p, ds->dim);
  if (ds->aux) copy(ds->aux, osave[i].aux, ds->aux_dim);

  ds->know_pot = 0;
  ds->know_grad = 0;
 
  if (N_quantities==-1)
  { printf("move %8.6lf %20.10le\n", ds->temp_state->inv_temp, 
                                     osort[above].value/(b1-b2));
  }
}
