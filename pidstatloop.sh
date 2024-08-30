#! /bin/bash

while true
do

	pidstat -r | tee -a pidstatOutput.txt
	sleep 300

done

