<?php
/*
	usage 1: php write_urls.php %total_url_num%
	usage 2: php write_urls.php %total_url_num% %start_item_no%
*/
$myFile = "test_urls.txt";
$fh = fopen($myFile, 'w') or die("can't open file");

$counter = 1000;
$start_no = 652405;

if (isset($argv[1]))
	$counter = $argv[1];
	
if (isset($argv[2]))
	$start_no = $argv[2];	
	
for ($i=0; $i<$counter; ++$i) {
	++$start_no;
	$url = "http://item.jd.com/$start_no.html".PHP_EOL;
	fwrite($fh, $url);
}

fclose($fh);
?>