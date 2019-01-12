export KDIR=/usr/src/kernel-4.18.5
export CURRENT_WORKING_FILES="$KDIR/include/linux/sched.h \
							    $KDIR/include/linux/sched/cputime.h \
							    $KDIR/include/linux/kernel.h \
							    $KDIR/include/linux/pid.h \
							    $KDIR/include/linux/sched.h.h \
							    $KDIR/include/uapi/asm-generic/errno-base.h \
							    $KDIR/include/linux/mm.h \
							    $KDIR/include/linux/mm_types.h \
							    $KDIR/include/linux/cdev.h \
							    $KDIR/include/linux/kdev_t.h \
							    $KDIR/include/linux/uaccess.h"
emacs *.[chCH] *Makefile $KDIR/include/linux $CURRENT_WORKING_FILES e.sh
