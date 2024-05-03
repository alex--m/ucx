echo "==== Building UCX+OMPI+OSU (BUILD_PATH=$BUILD_PATH) ===="
. $(dirname "$0")/common.sh
cd $(dirname "$0")/../../..

# UCX Cleanup, which may fail (therefore preceeds "set -e")
make distclean
find . -name "*.la" | xargs rm
git clean -f -X -d

# Test the presence/location of intended sources
set -e
touch ./ompi
touch ./osu

echo "==== Building UCX (release) ===="
git clean -f -X -d # Remove all traces of the previous build...
./autogen.sh
./contrib/configure-opt --prefix=$BUILD_PATH
make
make install

echo "==== Building Open MPI (release) ===="
pushd ./ompi
./autogen.pl
./configure --prefix=$BUILD_PATH --with-platform=contrib/platform/mellanox/optimized --with-ucx=$BUILD_PATH --disable-man-pages
make
make install
popd
