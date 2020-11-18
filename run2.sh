#!/bin/bash
echo "Please, input your option:"
echo "0) See you again!\n
      1)  aturvolumetv.wav\n
      2)  lagumajutakgentar.wav\n
      3)  suratalbaqarah.wav\n
      4)  setalarm.wav\n
      5)  lampukamar.wav\n
      *)  Sorry, I don't understand\n"
echo "input :"

init=""
read INPUT_STRING
    case $INPUT_STRING in
        0)  echo "See you again!";;
        1)  init="aturvolumetv.wav";;
        2)  init="lagumajutakgentar.wav";;
        3)  init="suratalbaqarah.wav";;
        4)  init="setalarm.wav";;
        5)  init="lampukamar.wav";;
        *)  echo "Sorry, I don't understand";;
    esac

if [ ${#init} == 0 ] 
then
    exit 0
else
    echo "client-ss-websocket-lite -f $init"
    ./client-ss-websocket-lite -f $init
fi

