#!/usr/bin/perl -w
use Time::Local

open (ELTAKO, "eltako.log");

my $oldEpoch = -1;
my $maxObsc = 0, $maxWind = 0, $maxRain = 0;
while ($record = <ELTAKO>) {
     $record =~ /^((\d\d\d\d)-(\d\d)-(\d\d)\s(\d\d):(\d\d):(\d\d))\st([+-]\d\d\.\d)s(\d\d)w(\d\d)e(\d\d)([oO])d(\d\d\d)v(\d\d\.\d)([rR])$/;
     my $epoch = timelocal($7,$6,$5,$4,$3-1,$2);

     $accuTemp += $8;
     $accuSunS += $9;
     $accuSunW += $10;
     $accuSunE += $11;
     $maxObsc = ($12 eq "O") ? 1 : $maxObsc;
     $accuDawn += $13;
     $maxWind = ($14 gt $maxWind) ? $14 : $maxWind;
     $maxRain = ($15 eq "R")? 1 : $maxRain;
     $counter++;

     if ($epoch % 300 < $oldEpoch) {
       my $avgTemp = $accuTemp / $counter;
       my $avgSunS = $accuSunS / $counter;
       my $avgSunW = $accuSunW / $counter;
       my $avgSunE = $accuSunE / $counter;
       my $avgDawn = $accuDawn / $counter;

       $data = sprintf ("%d:%+05.1f:%04.1f:%s:%2.2d:%2.2d:%2.2d:%3.3d:%s\n", $epoch, $avgTemp, $maxWind, $maxRain, $avgSunE, $avgSunS, $avgSunW, $avgDawn, $maxObsc);
       system("/usr/bin/rrdtool update weather.rrd $data");

       $counter = 0;
       $accuTemp = 0;
       $accuSunS = 0;
       $accuSunW = 0;
       $accuSunE = 0;
       $accuDawn = 0;
       $maxWind = 0;
       $maxRain = 0;
       $maxObsc = 0;
     }
     $oldEpoch = $epoch % 300;

}

close (ELTAKO)
