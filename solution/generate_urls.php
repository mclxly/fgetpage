<?php
/***********************************************************
	Project Name: Compare Price
	Description: Generate urls for product pages.
	Created: May 11, 2013
	Last Updated:
	History:
***********************************************************/
// --------------------------------------
// global var
$pipe = "urls_list.fifo";
$mode = 0600;
//$mode = 0777;

$counter = 10;
$start_no = 652405;

// --------------------------------------
if(!file_exists($pipe)) {
	// create the pipe
	umask(0);
	posix_mkfifo($pipe,$mode);
}

if (isset($argv[1]))
	$counter = $argv[1];
	
if (isset($argv[2]))
	$start_no = $argv[2];	

$fh = fopen($pipe,"w");
echo 'start writing urls...'.PHP_EOL;	
for ($i=0; $i<$counter; ++$i) {
	++$start_no;
	$url = "http://item.jd.com/$start_no.html".PHP_EOL;
	fwrite($fh, $url);	//block until there is a reader
}

//unlink($pipe); //delete pipe	 
?>