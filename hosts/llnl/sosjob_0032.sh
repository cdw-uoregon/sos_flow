#!/bin/bash
#MSUB -V -j oe
#MSUB -l nodes=33
#MSUB -l walltime=00:15:00

####
#
#  Launch the SOS runtime:
#
#  Verify the environment has been configured:
if [ "x$SOS_ENV_SET" == "x" ] ; then
	echo "Please set up your SOS environment first."
    kill -INT $$
fi
if ls $SOS_EVPATH_MEETUP/sosd.*.key 1> /dev/null 2>&1
then
    echo "WARNING: Aggregator KEY file[s] exist already.  Deleting them."
    rm -f $SOS_EVPATH_MEETUP/sosd.*.key
fi
if ls $SOS_WORK/sosd.*.db 1> /dev/null 2>&1
then
    echo "WARNING: SOSflow DATABASE file[s] exist already.  Deleting them."
    evp.cleanall
fi
#
echo ""
echo "Launching SOS daemons..."
echo "    sosd(0) @ 33 -- aggregator"
srun \
    -n=1 \
    -dist=cyclic \
    -r=32 \
    ${SOS_BUILD_DIR}/bin/sosd \
        -l 32 \
        -a 1 \
        -w ${SOS_WORK} \
        -k 0 \
        -r aggregator \
    &
#
#  Listeners:
for SOS_RANK in {1..32}
do
    OFFSET=$(expr $SOS_RANK - 1)
    echo "    sosd($SOS_RANK) @ $OFFSET -- listener"
    srun \
        -n=1 \
        -dist=cyclic \
        -r=$OFFSET \
        ${SOS_BUILD_DIR}/bin/sosd \
            -l 32 \
            -a 1 \
            -w ${SOS_WORK} \
            -k ${THIS_RANK} \
            -r listener \
        &
done
echo "Pausing to ensure runtime is completely established..."
sleep 5 
echo "SOS is now online."
#
#### -------------------------------------------------------------------------
#vvv
#vvv  --- INSERT YOUR EXPERIMENT CODE HERE ---
#vv
#v

srun -n=32 -r=0 ${SOS_BUILD_DIR}/bin/demo_app -i 1 -p 5 -m 25

#^
#^^
#^^^
#^^^
####
#
#  Bring the SOS runtime down cleanly:
#
srun -n=32 -r=0 ${SOS_BUILD_DIR}/bin/sosd_stop
#
####
