/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fix_gravity.h"
#include "atom.h"
#include "update.h"
#include "domain.h"
#include "respa.h"
#include "modify.h"
#include "input.h"
#include "variable.h"
#include "math_const.h"
#include "fix_multisphere.h"  //NP modified C.K.
#include "error.h"
#include "force.h"

using namespace LAMMPS_NS;
using namespace FixConst;
using namespace MathConst;

enum{CHUTE,SPHERICAL,VECTOR};
enum{CONSTANT,EQUAL};

#define BIG 1.0e20
/* ---------------------------------------------------------------------- */

FixGravity::FixGravity(LAMMPS *lmp, int narg, char **arg) :
  Fix(lmp, narg, arg)
{
  if (narg < 5) error->all(FLERR,"Illegal fix gravity command");

  scalar_flag = 1;
  global_freq = 1;
  extscalar = 1;

  mstr = vstr = pstr = tstr = xstr = ystr = zstr = NULL;
  mstyle = vstyle = pstyle = tstyle = xstyle = ystyle = zstyle = CONSTANT;

  //~ Initialise at ridiculous values for later detection
  inletpiperad = BIG;
  inletpipey = -BIG;
  
  if (strstr(arg[3],"v_") == arg[3]) {
    int n = strlen(&arg[3][2]) + 1;
    mstr = new char[n];
    strcpy(mstr,&arg[3][2]);
    mstyle = EQUAL;
  } else {
    magnitude = force->numeric(FLERR,arg[3]);
    mstyle = CONSTANT;
  }

  if (strcmp(arg[4],"chute") == 0) {
    if (narg != 6) error->all(FLERR,"Illegal fix gravity command");
    style = CHUTE;
    if (strstr(arg[5],"v_") == arg[5]) {
      int n = strlen(&arg[5][2]) + 1;
      vstr = new char[n];
      strcpy(vstr,&arg[5][2]);
      vstyle = EQUAL;
    } else {
      vert = force->numeric(FLERR,arg[5]);
      vstyle = CONSTANT;
    }

  } else if (strcmp(arg[4],"spherical") == 0) {
    if (narg != 7) error->all(FLERR,"Illegal fix gravity command");
    style = SPHERICAL;
    if (strstr(arg[5],"v_") == arg[5]) {
      int n = strlen(&arg[5][2]) + 1;
      pstr = new char[n];
      strcpy(pstr,&arg[5][2]);
      pstyle = EQUAL;
    } else {
      phi = force->numeric(FLERR,arg[5]);
      pstyle = CONSTANT;
    }
    if (strstr(arg[6],"v_") == arg[6]) {
      int n = strlen(&arg[6][2]) + 1;
      tstr = new char[n];
      strcpy(tstr,&arg[6][2]);
      tstyle = EQUAL;
    } else {
      theta = force->numeric(FLERR,arg[6]);
      tstyle = CONSTANT;
    }

  } else if (strcmp(arg[4],"vector") == 0) {
    if (narg != 8 && narg != 10) error->all(FLERR,"Illegal fix gravity command");
    style = VECTOR;

    int argnum = 5;
    if (narg == 10) {//~ Update if realistic values are defined
      inletpiperad = force->numeric(FLERR,arg[5]);
      inletpipey = force->numeric(FLERR,arg[6]);
      argnum += 2;
    }
    
    if (strstr(arg[argnum],"v_") == arg[argnum]) {
      int n = strlen(&arg[argnum][2]) + 1;
      xstr = new char[n];
      strcpy(xstr,&arg[argnum][2]);
      xstyle = EQUAL;
    } else {
      xdir = force->numeric(FLERR,arg[argnum]);
      xstyle = CONSTANT;
    }
    argnum++;
    if (strstr(arg[argnum],"v_") == arg[argnum]) {
      int n = strlen(&arg[argnum][2]) + 1;
      ystr = new char[n];
      strcpy(ystr,&arg[argnum][2]);
      ystyle = EQUAL;
    } else {
      ydir = force->numeric(FLERR,arg[argnum]);
      ystyle = CONSTANT;
    }
    argnum++;
    if (strstr(arg[argnum],"v_") == arg[argnum]) {
      int n = strlen(&arg[argnum][2]) + 1;
      zstr = new char[n];
      strcpy(zstr,&arg[argnum][2]);
      zstyle = EQUAL;
    } else {
      zdir = force->numeric(FLERR,arg[argnum]);
      zstyle = CONSTANT;
    }

  } else error->all(FLERR,"Illegal fix gravity command");

  degree2rad = MY_PI/180.0;
  time_origin = update->ntimestep;

  eflag = 0;
  egrav = 0.0;

  fm = NULL; //NP modified C.K.
}

/* ---------------------------------------------------------------------- */

FixGravity::~FixGravity()
{
  delete [] mstr;
  delete [] vstr;
  delete [] pstr;
  delete [] tstr;
  delete [] xstr;
  delete [] ystr;
  delete [] zstr;
}

/* ---------------------------------------------------------------------- */

int FixGravity::setmask()
{
  int mask = 0;
  mask |= POST_FORCE;
  mask |= THERMO_ENERGY;
  mask |= POST_FORCE_RESPA;
  return mask;
}

/* ---------------------------------------------------------------------- */

void FixGravity::init()
{
  if (strstr(update->integrate_style,"respa"))
    nlevels_respa = ((Respa *) update->integrate)->nlevels;

  // check variables

  if (mstr) {
    mvar = input->variable->find(mstr);
    if (mvar < 0)
      error->all(FLERR,"Variable name for fix gravity does not exist");
    if (!input->variable->equalstyle(mvar))
      error->all(FLERR,"Variable for fix gravity is invalid style");
  }
  if (vstr) {
    vvar = input->variable->find(vstr);
    if (vvar < 0)
      error->all(FLERR,"Variable name for fix gravity does not exist");
    if (!input->variable->equalstyle(vvar))
      error->all(FLERR,"Variable for fix gravity is invalid style");
  }
  if (pstr) {
    pvar = input->variable->find(pstr);
    if (pvar < 0)
      error->all(FLERR,"Variable name for fix gravity does not exist");
    if (!input->variable->equalstyle(pvar))
      error->all(FLERR,"Variable for fix gravity is invalid style");
  }
  if (tstr) {
    tvar = input->variable->find(tstr);
    if (tvar < 0)
      error->all(FLERR,"Variable name for fix gravity does not exist");
    if (!input->variable->equalstyle(tvar))
      error->all(FLERR,"Variable for fix gravity is invalid style");
  }
  if (xstr) {
    xvar = input->variable->find(xstr);
    if (xvar < 0)
      error->all(FLERR,"Variable name for fix gravity does not exist");
    if (!input->variable->equalstyle(xvar))
      error->all(FLERR,"Variable for fix gravity is invalid style");
  }
  if (ystr) {
    yvar = input->variable->find(ystr);
    if (yvar < 0)
      error->all(FLERR,"Variable name for fix gravity does not exist");
    if (!input->variable->equalstyle(yvar))
      error->all(FLERR,"Variable for fix gravity is invalid style");
  }
  if (zstr) {
    zvar = input->variable->find(zstr);
    if (zvar < 0)
      error->all(FLERR,"Variable name for fix gravity does not exist");
    if (!input->variable->equalstyle(zvar))
      error->all(FLERR,"Variable for fix gravity is invalid style");
  }

  varflag = CONSTANT;
  if (mstyle != CONSTANT || vstyle != CONSTANT || pstyle != CONSTANT ||
      tstyle != CONSTANT || xstyle != CONSTANT || ystyle != CONSTANT ||
      zstyle != CONSTANT) varflag = EQUAL;

  // set gravity components once and for all

  if (varflag == CONSTANT) set_acceleration();

  //NP modified C.K. check if a fix multisphere is registered
  fm = NULL;
  int nms = modify->n_fixes_style("multisphere");
  if(nms > 1)
    error->fix_error(FLERR,this,"support for more than one fix multisphere not implemented");
  if(nms)
    fm = static_cast<FixMultisphere*>(modify->find_fix_style("multisphere",0));
}

/* ---------------------------------------------------------------------- */

void FixGravity::setup(int vflag)
{
  if (strstr(update->integrate_style,"verlet"))
    post_force(vflag);
  else {
    ((Respa *) update->integrate)->copy_flevel_f(nlevels_respa-1);
    post_force_respa(vflag,nlevels_respa-1,0);
    ((Respa *) update->integrate)->copy_f_flevel(nlevels_respa-1);
  }
}

/* ---------------------------------------------------------------------- */

void FixGravity::post_force(int vflag)
{
  // update gravity due to variables

  if (varflag != CONSTANT) {
    modify->clearstep_compute();
    if (mstyle == EQUAL) magnitude = input->variable->compute_equal(mvar);
    if (vstyle == EQUAL) vert = input->variable->compute_equal(vvar);
    if (pstyle == EQUAL) phi = input->variable->compute_equal(pvar);
    if (tstyle == EQUAL) theta = input->variable->compute_equal(tvar);
    if (xstyle == EQUAL) xdir = input->variable->compute_equal(xvar);
    if (ystyle == EQUAL) ydir = input->variable->compute_equal(yvar);
    if (zstyle == EQUAL) zdir = input->variable->compute_equal(zvar);
    modify->addstep_compute(update->ntimestep + 1);

    set_acceleration();
  }

  double **x = atom->x;
  double **f = atom->f;
  double *rmass = atom->rmass;
  double *mass = atom->mass;
  int *mask = atom->mask;
  int *type = atom->type;
  int nlocal = atom->nlocal;
  double massone, r;

  eflag = 0;
  egrav = 0.0;

  if (rmass) {
    for (int i = 0; i < nlocal; i++)
      if ((mask[i] & groupbit) && (!fm || (fm && fm->belongs_to(i) < 0))) { //NP modified C.K.
	r = sqrt(x[i][0]*x[i][0] + x[i][2]*x[i][2]);
	if (r > inletpiperad || x[i][1] < inletpipey) {
	  massone = rmass[i];
	  f[i][0] += massone*xacc;
	  f[i][1] += massone*yacc;
	  f[i][2] += massone*zacc;
	  egrav -= massone * (xacc*x[i][0] + yacc*x[i][1] + zacc*x[i][2]);
	}
      }
  } else {
    for (int i = 0; i < nlocal; i++)
      if ((mask[i] & groupbit) && (!fm || (fm && fm->belongs_to(i) < 0))) { //NP modified C.K.
	r = sqrt(x[i][0]*x[i][0] + x[i][2]*x[i][2]);
	if (r > inletpiperad || x[i][1] < inletpipey) {
	  massone = mass[type[i]];
	  f[i][0] += massone*xacc;
	  f[i][1] += massone*yacc;
	  f[i][2] += massone*zacc;
	  egrav -= massone * (xacc*x[i][0] + yacc*x[i][1] + zacc*x[i][2]);
	}
      }
  }
}

/* ---------------------------------------------------------------------- */

void FixGravity::post_force_respa(int vflag, int ilevel, int iloop)
{
  if (ilevel == nlevels_respa-1) post_force(vflag);
}

/* ---------------------------------------------------------------------- */

void FixGravity::set_acceleration()
{
  if (style == CHUTE || style == SPHERICAL) {
    if (style == CHUTE) {
      phi = 0.0;
      theta = 180.0 - vert;
    }
    if (domain->dimension == 3) {
      xgrav = sin(degree2rad * theta) * cos(degree2rad * phi);
      ygrav = sin(degree2rad * theta) * sin(degree2rad * phi);
      zgrav = cos(degree2rad * theta);
    } else {
      xgrav = sin(degree2rad * theta);
      ygrav = cos(degree2rad * theta);
      zgrav = 0.0;
    }
  } else if (style == VECTOR) {
    if (domain->dimension == 3) {
      double length = sqrt(xdir*xdir + ydir*ydir + zdir*zdir);
      if(length == 0.)
        error->one(FLERR,"Gravity direction vector = 0");
      xgrav = xdir/length;
      ygrav = ydir/length;
      zgrav = zdir/length;
    } else {
      double length = sqrt(xdir*xdir + ydir*ydir);
      if(length == 0.)
        error->one(FLERR,"Gravity direction vector = 0");
      xgrav = xdir/length;
      ygrav = ydir/length;
      zgrav = 0.0;
    }
  }

  xacc = magnitude*xgrav;
  yacc = magnitude*ygrav;
  zacc = magnitude*zgrav;
}

/* ----------------------------------------------------------------------
   potential energy in gravity field
------------------------------------------------------------------------- */

double FixGravity::compute_scalar()
{
  // only sum across procs one time

  if (eflag == 0) {
    MPI_Allreduce(&egrav,&egrav_all,1,MPI_DOUBLE,MPI_SUM,world);
    eflag = 1;
  }
  return egrav_all;
}


/* ---------------------------------------------------------------------- */
//NP modified C.K.

void FixGravity::get_gravity(double *grav)
{
    grav[0] = xgrav * magnitude;
    grav[1] = ygrav * magnitude;
    grav[2] = zgrav * magnitude;
}
