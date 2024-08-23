#!/bin/bash

FILE=""
SFTP=""
DEST=""
DEBG=""

# Set these variables for your own system for uploading to a remote host
SFTP_ADDR=""
SFTP_PORT=""
SFTP_USER=""
SFTP_FLDR=""

while getopts "f:s:d:D" option; do
	case $option in
		f) 	FILE=$OPTARG;;
		s) 	SFTP=1;;
		d)	DEST=$OPTARG;;
  		D)	DEBG=$OPTARG;;
		\?)	echo "Usage: compile -f [filename] [-s] [-d destination] [-D debug symbol]";;
	esac
done

COMPILER=sourcemod/scripting/spcomp

if [ ! -d "sourcemod/scripting" ] || [ ! -d "sourcemod/plugins" ]; then
    	echo "Sourcemod plugin folders not found"
    	exit 1
fi

echo "${FILE}.sp compile `date`" > compile.log

if [ "$DEBG" ]; then
	${COMPILER} sourcemod/scripting/${FILE}.sp --show-stats -h ${DEBG}= --use-stderr >> compile.log
else
	${COMPILER} sourcemod/scripting/${FILE}.sp --show-stats -h --use-stderr >> compile.log
fi
 
if [ -f ${FILE}.smx ]; then
    	if [ -f sourcemod/plugins/${FILE}.smx ]; then
        	mv --backup=numbered sourcemod/plugins/${FILE}.smx sourcemod/plugins/disabled/
    	fi
    
    	if [ "$SFTP" ]; then
		sftp -P ${SFTP_PORT} ${SFTP_USER}@${SFTP_ADDR}:${SFTP_FLDR} <<EOF
			put ${FILE}.smx
			quit
EOF
    	fi

	if [ "$DEST" ]; then
    		mv ${FILE}.smx ${DEST}
	else
    		mv ${FILE}.smx ./sourcemod/plugins/
	fi
else
    	echo "${COMPILER} failed"
fi
