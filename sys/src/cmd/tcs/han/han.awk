function dec(x,		i, j, c){
	j = 0
	for(i = 1; i <= length(x); i++){
		c = substr(x, i, 1)
		j = j*16 + dig[c]
		if(dig[c] == "") print "bad hex:", x > "/fd/2"
	}
	return(j)
}
BEGIN	{
		FS = "	"
		dig["0"] = 0;dig["1"] = 1;dig["2"] = 2;dig["3"] = 3;
		dig["4"] = 4;dig["5"] = 5;dig["6"] = 6;dig["7"] = 7;
		dig["8"] = 8;dig["9"] = 9;dig["a"] = 10;dig["b"] = 11;
		dig["c"] = 12;dig["d"] = 13;dig["e"] = 14;dig["f"] = 15;
		dig["A"] = 10;dig["B"] = 11;
		dig["C"] = 12;dig["D"] = 13;dig["E"] = 14;dig["F"] = 15;
		nskip = 0
	}
	{
		x = dec($1)
		if(x < 16384) print "warning:" NR ":", $1, " < 0x4000" > "/fd/2"
		for(i = 2; i <= NF; i++) {
			if(index($i, "X-")){ nskip++; continue; }
			printf "%d %d\t\t# k%s 0x%x\n", $i+0, x, $i, x
			x++
		}
	}
END	{
		if(nskip) print nskip, "JIS 212 chars skipped" > "/fd/2"
	}
