#install.sh

cd ..
sh autogen.sh
echo "./autogen.sh completed..."


echo "Do you want to configure in debugging mode (y/n) ? "
read debug
if [ $debug == 'y' ]
then
        sh configure --enable-debug
elif [ $debug == 'n' ]
then
        sh configure
else
        echo "wrong choice"  
     #better loop this set of statements in case wrong choice is entered.
fi


echo "Do you want to run 'make' without optimization (y/n) ? "
read opti
if [ $opti == 'y' ]
then
        make -j4 CFLAGS="-ggdb -O0"
elif [ $opti == 'n' ]
then
        make
else
        echo "wrong choice"
fi

sudo make install
                 
