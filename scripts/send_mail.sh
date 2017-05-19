./install.sh > make_output.txt
get_status=`tail -5 make_output.txt`
echo $get_status > status.txt
if grep -q 'ERROR\|error\|Error' status.txt
then
	echo "Build not successful"
else
	echo "Successful"
	echo "The build has been successful" | mail -s "Build Result" spandit@redhat.com
	echo "Mail has been sent to sachinspandits@gmail.com"
fi
