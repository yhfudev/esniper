function getTime(artliste) {
	c1 = new Image(); c1.src = "1c.gif";
	c2 = new Image(); c2.src = "2c.gif";
	c3 = new Image(); c3.src = "3c.gif";
	c4 = new Image(); c4.src = "4c.gif";
	c5 = new Image(); c5.src = "5c.gif";
	c6 = new Image(); c6.src = "6c.gif";
	c7 = new Image(); c7.src = "7c.gif";
	c8 = new Image(); c8.src = "8c.gif";
	c9 = new Image(); c9.src = "9c.gif";
	c0 = new Image(); c0.src = "0c.gif";
	Cc = new Image(); Cc.src = "Cc.gif";
	now = new Date();
	for(i=0;i<artliste.length;i++) {	
		later = new Date(artliste[i][3],artliste[i][2]-1,artliste[i][1],artliste[i][4],artliste[i][5],artliste[i][6]);
		if (later>now) {
			days = (later - now) / 1000 / 60 / 60 / 24;
			daysRound = Math.floor(days);
			hours = (later - now) / 1000 / 60 / 60 - (24 * daysRound);
			hoursRound = Math.floor(hours);
			minutes = (later - now) / 1000 /60 - (24 * 60 * daysRound) - (60 * hoursRound);
			minutesRound = Math.floor(minutes);
			seconds = (later - now) / 1000 - (24 * 60 * 60 * daysRound) - (60 * 60 * hoursRound) - (60 * minutesRound);
			secondsRound = Math.round(seconds);

			if (secondsRound <= 9) {
			document.getElementById("g"+artliste[i][0]).src = c0.src;
			document.getElementById("h"+artliste[i][0]).src = eval("c"+secondsRound+".src");
			}
			else {
			document.getElementById("g"+artliste[i][0]).src = eval("c"+Math.floor(secondsRound/10)+".src");
			document.getElementById("h"+artliste[i][0]).src = eval("c"+(secondsRound%10)+".src");
			}
			if (minutesRound <= 9) {
			document.getElementById("d"+artliste[i][0]).src = c0.src;
			document.getElementById("e"+artliste[i][0]).src = eval("c"+minutesRound+".src");
			}
			else {
			document.getElementById("d"+artliste[i][0]).src = eval("c"+Math.floor(minutesRound/10)+".src");
			document.getElementById("e"+artliste[i][0]).src = eval("c"+(minutesRound%10)+".src");
			}
			if (hoursRound <= 9) {
			document.getElementById("y"+artliste[i][0]).src = c0.src;
			document.getElementById("z"+artliste[i][0]).src = eval("c"+hoursRound+".src");
			}
			else {
			document.getElementById("y"+artliste[i][0]).src = eval("c"+Math.floor(hoursRound/10)+".src");
			document.getElementById("z"+artliste[i][0]).src = eval("c"+(hoursRound%10)+".src");
			}
			if (daysRound <= 9) {
			document.getElementById("x"+artliste[i][0]).src = c0.src;
			document.getElementById("a"+artliste[i][0]).src = c0.src;
			document.getElementById("b"+artliste[i][0]).src = eval("c"+daysRound+".src");
			}
			if (daysRound <= 99) {
			document.getElementById("x"+artliste[i][0]).src = c0.src;
			document.getElementById("a"+artliste[i][0]).src = eval("c"+Math.floor((daysRound/10)%10)+".src");
			document.getElementById("b"+artliste[i][0]).src = eval("c"+Math.floor(daysRound%10)+".src");
			}
			if (daysRound <= 999){
			document.getElementById("x"+artliste[i][0]).src = eval("c"+Math.floor(daysRound/100)+".src");
			document.getElementById("a"+artliste[i][0]).src = eval("c"+Math.floor((daysRound/10)%10)+".src");
			document.getElementById("b"+artliste[i][0]).src = eval("c"+Math.floor(daysRound%10)+".src");
			}
		}
	}
}
