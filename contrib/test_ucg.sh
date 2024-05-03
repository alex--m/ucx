#!/bin/sh -eE
#
# This script was written for testing UCG: collective operations in UCX.
# It either builds or uses an existing git clone of the UCX and Open MPI
# repositories to run the tests, optionally on a cluster managed by SLURM.
#
# Example of a new installation (including cloning from scratch):
# $> /tmp/test_ucg.sh -s sources -c -r alex--m -j -l localhost -x xpmem
#
# Examples for existing installations:
# $> pushd nova00
# $> ~/ucx/contrib/test_ucg.sh -j -p amd -n nova00 -t 4-00:00 -x xpmem
# $> ~/ucx/contrib/test_ucg.sh -j -p amd -n nova00 -t 4-00:00 -x xpmem -m 32
# $> ~/ucx/contrib/test_ucg.sh -j -p amd -n nova00 -t 4-00:00 -x xpmem -m 16
# $> popd
# $> pushd thundeclap
# $> ~/ucx/contrib/test_ucg.sh -o -j -p amd -n thunderclap -t 4-00:00 -x xpmem
# $> ~/ucx/contrib/test_ucg.sh -o -j -p amd -n thunderclap -t 4-00:00 -x xpmem -m 32
# $> ~/ucx/contrib/test_ucg.sh -o -j -p amd -n thunderclap -t 4-00:00 -x xpmem -m 16
# $> popd
# $> pushd aarch64
# $> ~/ucx/contrib/test_ucg.sh -o -j -p arm -t 8-00:00
# $> popd
#

HOST_LIST="localhost"         # comma-delimited list of servers to run on
SOURCE_PATH="${HOME}"         # Use UCX and MPI sources from this location
SOURCE_CLONE=0                # Whether to clone sources if not found
BUILD_OPT=0                   # Whether to build with extra optimizations
MAX_PPN=${SLURM_CPUS_ON_NODE} # PPN cannot exceed SLURM limit (when in SLURM)

# Show script usage help message
usage()
{
    echo " Usage:"
    echo "  "$0" [ options ]"
    echo " Options:"
    echo "  -s <path>             Path to UCX and MPI sources (default: ${DEFAULT_SOURCE_PATH})"
    echo "  -c                    Clone UCX and MPI sources if not found"
    echo "  -r <override-repo>    Use this Github user/organization, overriding the default repository"
    echo "  -o                    Include optimizations, e.g. SSE-4.2, not AVX512 (otherwise: regular release build)"
    echo "  -j                    Parallel builds (passed to 'make')"
    echo "  -t <slurm-timeout>    Test using a SLURM job for this much time (DD-HH:MM)"
    echo "  -p <slurm-partition>  Test using a SLURM job on this partition"
    echo "  -n <host-list>        Run MPI on this list of hosts (ask SLURM for it if -p is also used)"
    echo "  -m <max-ppn>          Run MPI with up to this many processes per node"
    echo "  -x <extra-transport>  UCX transport to use for evaluation (requires -m)"
    echo "  -i <imbalance>        Job imbalance to simulate, in microseconds (requires -x)"
    echo ""
    exit 1
}

# Parse script's command-line arguments
while getopts ":s:cr:ojt:p:n:l:m:x:i:" o; do
    case "${o}" in
    s)
        SOURCE_PATH=${OPTARG}
        ;;
    c)
        SOURCE_CLONE=1
        ;;
    r)
        OVERRIDE_REPOS=${OPTARG}
        ;;
    o)
        BUILD_OPT=1
        ;;
    j)
        BUILD_PARALLEL="-j"
        ;;
    t)
        SLURM_TIMEOUT="#SBATCH --time=${OPTARG}"
        ;;
    p)
        SLURM_PARTITION="#SBATCH --partition=${OPTARG}"
        ;;
    n)
        HOST_LIST=${OPTARG}
        SLURM_NODE="#SBATCH --nodelist=${OPTARG}"
        ;;
    m)
        NEWLINE=$'\n'
        MAX_PPN=${OPTARG}
        SLURM_PPN="#SBATCH --ntasks=1${NEWLINE}#SBATCH --cpus-per-task=${OPTARG}"
        ;;
    x)
        EXTRA_TRASPORT=${OPTARG}
        ;;
    i)
        if [ -z ${EXTRA_TRASPORT+x} ]; then
            echo "Error: -i requires -x"
            usage
        fi
        IMBALANCE=${OPTARG}
        ;;
    *)
        usage
        ;;
    esac
done
shift $((OPTIND-1))

stash_and_rebase() {
    DIFF=$(git diff)
    if [ -z "${DIFF}" ]; then
        git pull --rebase origin `git branch --show-current`
    else
        git stash
        git pull --rebase origin `git branch --show-current`
        git stash pop
    fi
}

build_ucx() {
    if [ ! -d "${SOURCE_PATH}/ucx" ]; then
        if [ ${SOURCE_CLONE} -eq 0 ]; then
            echo "Failed: UCX not found at ${SOURCE_PATH}/ucx"
            exit 1
        fi

        if [ -z ${OVERRIDE_REPOS+x} ]; then
            UCX_REPO=${OVERRIDE_REPOS}
        else
            UCX_REPO="openucx"
        fi

        git clone git@github.com:${UCX_REPO}/ucx.git ${SOURCE_PATH}/ucx
        pushd ${SOURCE_PATH}/ucx
        ./autogen.sh
        popd
    else
        pushd ${SOURCE_PATH}/ucx
        stash_and_rebase
        popd
    fi

    if [ ! -d ucx ]; then
        mkdir ucx
        pushd ucx


        CONFIGURE_ARGS="--prefix=`pwd`/build --with-sm-coll --with-sm-coll-extra --without-avx512f" # --with-mcast-coll
        if [[ ${BUILD_OPT} -ne 0 ]]; then
            CFLAGS=-mno-avx512f ${SOURCE_PATH}/ucx/contrib/configure-opt ${CONFIGURE_ARGS}
        else
            ${SOURCE_PATH}/ucx/contrib/configure-release ${CONFIGURE_ARGS}
        fi
    else
        pushd ucx
    fi

    make ${BUILD_PARALLEL} install
    popd
}

build_ucc() {
    if [ ! -d "${SOURCE_PATH}/ucc" ]; then
        if [ ${SOURCE_CLONE} -eq 0 ]; then
            echo "Failed: UCC not found at ${SOURCE_PATH}/ucc"
            exit 1
        fi

        git clone git@github.com:openucx/ucc.git ${SOURCE_PATH}/ucc
        pushd ${SOURCE_PATH}/ucc
        popd
    else
        pushd ${SOURCE_PATH}/ucc
        stash_and_rebase
        popd
    fi

    if [ ! -d ucc ]; then
        mkdir ucc
        pushd ucc
        ${SOURCE_PATH}/ucc/configure --prefix=`pwd`/build --with-ucx=`pwd`/../ucx/build
    else
        pushd ucc
    fi

    make ${BUILD_PARALLEL} install
    popd
}

build_ompi() {
    if [ ! -d "${SOURCE_PATH}/ompi" ]; then
        if [ ${SOURCE_CLONE} -eq 0 ]; then
            echo "Failed: Open MPI not found at ${SOURCE_PATH}/ompi"
            exit 1
        fi

        if [ ! -z ${OVERRIDE_REPOS+x} ]; then
            OMPI_REPO=${OVERRIDE_REPOS}
        else
            OMPI_REPO="open-mpi"
        fi

        git clone git@github.com:${UCX_REPO}/ompi.git ${SOURCE_PATH}/ompi
        git submodule update --init --recursive
        pushd ${SOURCE_PATH}/ompi
        ./autogen.pl
        popd
    else
        pushd ${SOURCE_PATH}/ompi
        stash_and_rebase
        popd
    fi

    if [ ! -d ompi ]; then
        mkdir ompi
        pushd ompi
        ${SOURCE_PATH}/ompi/configure --prefix=`pwd`/build \
            --with-platform=${SOURCE_PATH}/ompi/contrib/platform/mellanox/optimized \
            --with-prte-platform=${SOURCE_PATH}/ompi/3rd-party/prrte/contrib/platform/mellanox/optimized \
            --with-ucx=`pwd`/../ucx/build \
            --with-ucc=`pwd`/../ucc/build \
            --with-libevent=internal \
            --disable-mpi-ext --enable-alt-short-float=long
    else
        pushd ompi
    fi

    make ${BUILD_PARALLEL} install
    popd
}

build_osu() {
    if [ ! -d "${SOURCE_PATH}/osu" ]; then
        if [ ${SOURCE_CLONE} -eq 0 ]; then
            echo "Failed: OSU not found at ${SOURCE_PATH}/osu"
            exit 1
        fi

        if [ -z ${OVERRIDE_REPOS+x} ]; then
            echo "Failed: cloning OSU requires -r"
        fi

        git clone git@github.com:${OVERRIDE_REPOS}/osu.git ${SOURCE_PATH}/osu
        pushd ${SOURCE_PATH}/osu
        ./autoreconf -ivf
        popd
    fi

    if [ ! -d osu ]; then
        mkdir osu
        pushd osu
        CC=`pwd`/../ompi/build/bin/mpicc \
        CXX=`pwd`/../ompi/build/bin/mpicxx \
        ${SOURCE_PATH}/osu/configure --prefix=`pwd`/build
    else
        pushd osu
    fi

    make ${BUILD_PARALLEL} install
    popd
}

run_slurm() {
    cat > test_ucg.sbatch <<EOF
#!/bin/bash
#SBATCH --job-name=UCG
#SBATCH --exclusive
#SBATCH --mem=0
#SBATCH --output test_ucg.log
${SLURM_TIMEOUT}
${SLURM_PARTITION}
${SLURM_NODE}
${SLURM_PPN}

# Collect information about the machine
lshw
cat /proc/cpuinfo
cat /proc/meminfo

# Prepare the command-line arguments for the run
ARGS="-j -s ${SOURCE_PATH} -m ${MAX_PPN} -n \${SLURM_JOB_NODELIST}"
if [[ ${BUILD_OPT} -ne 0 ]]; then
    ARGS="-o \$ARGS"
fi
if [[ ${SOURCE_CLONE} -ne 0 ]]; then
    ARGS="-c \$ARGS"
fi
if [ ! -z ${EXTRA_TRASPORT+x} ]; then
    ARGS="\$ARGS -x ${EXTRA_TRASPORT}"
    if [ ! -z ${IMBALANCE+x} ]; then
        ARGS="\$ARGS -i ${IMBALANCE}"
    fi
fi

# Re-run the bash-script from within the SLURM job
cd \$SLURM_SUBMIT_DIR
CMD="$0 \$ARGS"
echo \${CMD}
\${CMD}

EOF

    sbatch test_ucg.sbatch
}

run_benchmarks() {
    if [ -z ${MAX_PPN+x} ]; then
        echo "Failed: max ppn (-m) missing"
        exit 1
    fi

    BENCHMARK_CMD="python3 ${SOURCE_PATH}/ucx/contrib/schedulers/common/coll.py `pwd` ${MAX_PPN} `pwd`/osu/build/libexec/osu-micro-benchmarks `pwd`/ompi/build/bin/mpirun ${HOST_LIST} ${EXTRA_TRASPORT} ${IMBALANCE}"

    echo ${BENCHMARK_CMD}
    ${BENCHMARK_CMD}
}

if [ -z ${SLURM_PARTITION+x} ] && [ -z ${SLURM_HOST+x} ]; then
    build_ucx
    build_ucc
    build_ompi
    build_osu
    run_benchmarks
else
    run_slurm
fi
