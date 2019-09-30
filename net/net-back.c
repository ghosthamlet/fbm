/* NET-BACK.C - Routine for backpropagating the "error" through the network. */

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
#include "net.h"


/* This module find the derivative of the "error" for a particular case
   with respect to the values of the hidden and input units, and the with
   respect to the summed input for the hidden units, given the derivative 
   with respect to the output units.  There are facilities for calculating 
   these derivatives only back to a certain layer.
*/


static void zero_derivatives (net_value *, int),
            sum_derivatives  (net_value *, int, net_value *, int, net_param *);


/* BACKPROPAGATE ERROR DERIVATIVES.  The first argument must contain the 
   values of all the units in the network.  The second must contain the
   derivatives of the "error" with respect to the output units (in g->o).
   These derivatives are backpropagated to give the derivatives of the
   error with respect to the other units in the network, and with respect
   to the summed input into the hidden units..  This is done back to hidden 
   layer 'start', or back all the way to the inputs if 'start' is -1. */

void net_back
( net_values *v,	/* Values for units in network */
  net_values *d,	/* Place to get output derivatives, and store others */
  int start,		/* Earliest layer to find derivatives for */
  net_arch *a,		/* Network architecture */
  net_params *w		/* Network parameters */
)
{
  int l, i;

  /* Backpropagate through hidden layers. */

  for (l = a->N_layers-1; l>=0 && l>=start; l--)
  { 
    zero_derivatives (d->h[l], a->N_hidden[l]);
    
    if (a->has_ho[l])
    { sum_derivatives (d->o, a->N_outputs, d->h[l], a->N_hidden[l], 
                       w->ho[l]);
    }

    if (l<a->N_layers-1 && a->has_hh[l])
    { sum_derivatives (d->s[l+1], a->N_hidden[l+1], d->h[l], a->N_hidden[l], 
                       w->hh[l]);
    }

    for (i = 0; i<a->N_hidden[l]; i++)
    { d->s[l][i] = (1 - v->h[l][i]*v->h[l][i]) * d->h[l][i];
    }
  }

  /* Backpropagate to input layer. */

  if (start<0)
  {
    zero_derivatives (d->i, a->N_inputs);

    if (a->has_io)
    { sum_derivatives (d->o, a->N_outputs, d->i, a->N_inputs, w->io);
    }
 
    for (l = 0; l<a->N_layers; a++)
    { if (a->has_ih[l])
      { sum_derivatives (d->s[l], a->N_hidden[l], d->i, a->N_inputs, w->ih[l]);
      }
    }
  }
}


/* ZERO DERIVATIVES.  Sets the derivatives with respect to a set of source
   units to zero. */

static void zero_derivatives
( net_value *d,		/* Derivatives with respect to units to zero */
  int n			/* Number of units */
)
{
  int i;
  
  for (i = 0; i<n; i++)
  { d[i] = 0;
  }
}


/* SUM UP CONTRIBUTIONS TO THE DERIVATIVES FROM ONE GROUP OF CONNECTIONS.  Adds 
   the weighted sum of derivatives due to connections from source units to 
   a given destination layer to the totals for the source layer. */

static void sum_derivatives
( net_value *dd,	/* Derivatives with respect to destination units */
  int nd,		/* Number of destination units */
  net_value *ds,	/* Derivatives with respect to source units to add to */
  int ns,		/* Number of source units */
  net_param *w		/* Connection weights */
)
{
  net_value tv;
  int i, j;

  if (nd==1)
  { for (i = 0; i<ns; i++)
    { ds[i] += *w++ * dd[0];
    }
  }
  else
  {
    for (i = 0; i<ns; i++)
    { tv = *w++ * dd[0];
      j = 1;
      do { tv += *w++ * dd[j]; j += 1; } while (j<nd);
      ds[i] += tv;
    }
  }
}
