#install.sh

cd ..
sh autogen.sh
echo "./autogen.sh completed..."

sh configure --enable-debug
echo "./configure completed"

make -j4 CFLAGS="-ggdb -O0"
echo "make completed"

echo "installing make"
sudo make install
               
