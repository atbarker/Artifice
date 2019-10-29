#!/usr/bin/env Rscript
f <- file("stdin")
open(f)
while(length(line <- readLines(f, n=1)) > 0) {
    bartels.rank.text(line, two.sided, pvalue="normal")
    cox.stuart.text(line, two.sided)
    rank.test(line, two.sided)
    runs.text(line, two.sided)
    turning.point.test(line)

}
