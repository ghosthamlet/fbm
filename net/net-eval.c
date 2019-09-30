/* NET-EVAL.C - Program to evaluate the network function at a grid of points. */

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
#include "log.h"


#define Max_inputs 100		/* Maximum number of input dimensions allowed */

static void usage(void);


/* MAIN PROGRAM. */

void main
( int argc,
  char **argv
)
{
  net_arch   *a;
  net_priors *p;

  net_sigmas sigmas, *s = &sigmas;
  net_params params, *w = &params;
  net_values values, *v = &values;

  net_value *value_block;
  int value_count;

  net_value grid_low[Max_inputs];
  net_value grid_high[Max_inputs];

  int grid_size[Max_inputs];
  int grid_point[Max_inputs];

  log_file logf;
  log_gobbled logg;

  int lindex, hindex, index_mod;
  int ng;

  double *targets;
  int gen_targets;
  int N_targets;

  char **ap;
  int first;
  int i, j;

  /* Look at arguments. */
  
  gen_targets = 0;

  if (argc>1 && strcmp(argv[argc-1],"targets")==0)
  { gen_targets = 1;
    argc -= 1;
    argv[argc] = 0;
  }

  if (argc<7 || (argc-3)%4!=0 || (argc-3)/4>Max_inputs)
  { usage();
  }

  logf.file_name = argv[1];
  
  parse_range (argv[2], &lindex, &hindex, &index_mod);

  if (hindex==-2) 
  { hindex = lindex;
  }
  else if (hindex==-1)
  { hindex = 1000000000;
  }

  ng = 0;

  for (ap = argv+3; *ap!=0; ap += 4)
  { if (strcmp(ap[0],"/")!=0 || (grid_size[ng] = atoi(ap[3]))<=0) usage();
    grid_low[ng] = atof(ap[1]);
    grid_high[ng] = atof(ap[2]);
    ng += 1;
  }

  /* Open log file and read network architecture and priors. */

  log_file_open (&logf, 0);

  log_gobble_init(&logg,0);
  logg.req_size['A'] = sizeof *a;
  logg.req_size['P'] = sizeof *p;

  if (!logf.at_end && logf.header.index==-1)
  { log_gobble(&logf,&logg);
  }
  
  if ((a = logg.data['A'])==0)
  { fprintf(stderr,"No architecture specification in log file\n");
    exit(1);
  }

  if (a->N_inputs!=ng)
  { fprintf(stderr,
      "Number of grid ranges doesn't match number of input dimensions\n");
    exit(1);
  }

  s->total_sigmas = net_setup_sigma_count(a);
  w->total_params = net_setup_param_count(a);

  logg.req_size['S'] = s->total_sigmas * sizeof(net_sigma);
  logg.req_size['W'] = w->total_params * sizeof(net_param);

  if (gen_targets) 
  { 
    N_targets = net_model_targets(a);

    if ((p = logg.data['P'])==0)
    { fprintf(stderr,"No prior specification in log file\n");
      exit(1);
    }
  }

  /* Allocate space for values in network. */

  value_count = net_setup_value_count(a);
  value_block = chk_alloc (value_count, sizeof *value_block);

  net_setup_value_pointers (v, value_block, a);

  /* Evaluate function for the specified networks. */

  first = 1;

  for (;;)
  {
    /* Skip to next desired index, or to end of range. */
    
    while (!logf.at_end && logf.header.index<=hindex
     && (logf.header.index<lindex 
          || (logf.header.index-lindex)%index_mod!=0))
    { log_file_forward(&logf);
    }
  
    if (logf.at_end || logf.header.index>hindex)
    { break;
    }
  
    /* Gobble up records for this index. */

    log_gobble(&logf,&logg);
       
    /* Read the desired network from the log file. */
  
    if (logg.data['W']==0 || logg.index['W']!=logg.last_index)
    { fprintf(stderr,
        "No weights stored for the network with index %d\n",logg.last_index);
      exit(1);
    }
  
    w->param_block = logg.data['W'];
    net_setup_param_pointers (w, a);
  
    if (gen_targets) 
    {
      if (logg.data['S']==0 || logg.index['S']!=logg.last_index)
      { fprintf(stderr,
          "No sigmas stored for network with index %d\n",logg.last_index);
        exit(1);
      }
  
      s->sigma_block = logg.data['S'];
      net_setup_sigma_pointers (s, a);
    }
  
    /* Print the value of the network function, or targets generated from it, 
       for the grid of points. */

    if (first)
    { first = 0;
    }
    else
    { printf("\n");
    }
  
    for (i = 0; i<a->N_inputs; i++) 
    { grid_point[i] = 0;
      v->i[i] = grid_low[i];
    }
  
    for (;;)
    {
      net_func (v, 0, a, w);
  
      for (i = 0; i<a->N_inputs; i++) printf(" %8.5f",v->i[i]);
  
      if (gen_targets)
      { net_model_guess (v, targets, a, p, s, 1);
        for (j = 0; j<N_targets; j++) printf(" %+.6e",targets[j]);
      }
      else
      { for (j = 0; j<a->N_outputs; j++) printf(" %+.6e",v->o[j]);
      }
  
      printf("\n");
  
      for (i = a->N_inputs-1; i>=0 && grid_point[i]==grid_size[i]; i--) 
      { grid_point[i] = 0;
        v->i[i] = grid_low[i];
      }
   
      if (i<0) break;
  
      if (i!=a->N_inputs-1) printf("\n");
  
      grid_point[i] += 1;
      v->i[i] = grid_low[i] 
                  + grid_point[i] * (grid_high[i]-grid_low[i]) / grid_size[i];
    }
  }
  
  exit(0);
}


/* DISPLAY USAGE MESSAGE AND EXIT. */

static void usage(void)
{
  fprintf (stderr, 
  "Usage: net-eval log-file range { / low high grid-size } [ \"targets\" ]\n");
  exit(1);
}
