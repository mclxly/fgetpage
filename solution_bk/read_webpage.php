<?php
/*
	Description: Read webpage from FIFO.
*/
// ----------------------------------------
// global var
$tempFIFO = "urls_list.fifo";;

// ----------------------------------------
// info
echo 'PHP integer size: '.PHP_INT_SIZE.PHP_EOL;

// ----------------------------------------
// main
while (!file_exists($tempFIFO)) {
	echo 'waiting.'.PHP_EOL;
	sleep(2); // sleep 2 seconds
}
echo 'start reading'.PHP_EOL;

$fh = fopen($tempFIFO, 'r') or die("can't open file");
	
$binarydata = fread($fh, 2*PHP_INT_SIZE);
echo $binarydata;

$array = unpack("Lpid/Llen", $binarydata);
var_dump($array);

fclose($fh);
?>