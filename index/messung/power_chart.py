#!/usr/bin/env python
# coding=utf-8


import pylab as pl
import numpy as np


RASPBERRY = 0
if RASPBERRY:
	f = file("raspberry")
else:
	f = file("table")

head = [x.split()[0] for x in next(f).split("|")]
next(f)
a = [map(eval,l.split()[::2]) for l in f]


pl.figure(figsize=(10, 6), dpi=80)

Q = sorted(set((x[0],x[1],x[3]) for x in a if x[3] > 0))

xticks = []
for q in Q:
	X = [x[2] for x in a if (x[0],x[1],x[3]) == q]
	Y = [x[5] * x[4] / x[3] for x in a if (x[0],x[1],x[3]) == q]
	xticks = max(xticks, [0] + X)

	pl.plot(X, Y, "o-", label="CPUS: %d, threads: %d, input: %d MB" % q)


pl.xticks(xticks, [x if x%60==0 else "" for x in xticks])
pl.grid(True, which='major')
if RASPBERRY:
	pl.xlim(0, 700)
	pl.ylim(0, 2000)
else:
	pl.xlim(0, 1008)
	pl.ylim(0, 500)

pl.xlabel(u"Taktfrequenz in MHz")
pl.ylabel(ur"I*T/size  in mAs/MB")
pl.legend(loc='upper right', prop={"size": 8})
#pl.savefig("chart.png")
pl.show()