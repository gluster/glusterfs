./install.sh > make_output.txt
get_status=`tail -5 make_output.txt`
echo $get_status > status.txt
if grep -q 'ERROR\|error\|Error' status.txt
then
	echo "The build has been successful" | mail -s "Build Result" sachinspandits@gmail.com
else
	echo "Successful"
	echo "The buils has been successful" | mail -s "Buuld Result" sachinspandits@gmail.com 
fi
