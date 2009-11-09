/*
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright (c) 2003 Marcus R. Brown <mrbrown@0xd6.org>
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.
#
# $Id$
# Defines all IRX imports.
*/

#ifndef IOP_IRX_IMPORTS_H
#define IOP_IRX_IMPORTS_H

#include "irx.h"

/* Please keep these in alphabetical order!  */
#include "dmacman.h"
#include "intrman.h"
#ifdef DEV9X_DEV
#include "ioman.h"
#endif
#include "loadcore.h"
#ifdef POWEROFF
#include "poweroff.h"
#endif
#ifdef DEBUG
#include "stdio.h"
#endif
#include "thbase.h"
#include "thsemap.h"

#endif /* IOP_IRX_IMPORTS_H */
