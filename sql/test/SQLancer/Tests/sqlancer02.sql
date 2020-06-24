START TRANSACTION; -- Bug 6909
CREATE TABLE "sys"."t0" ("c0" BOOLEAN,"c1" DOUBLE PRECISION NOT NULL);
COPY 4 RECORDS INTO "sys"."t0" FROM stdin USING DELIMITERS E'\t',E'\n','"';
false	0.2059926383949241
NULL	1.89755202e+09
NULL	0.8514002985569765
NULL	0.8565135463082767

SELECT ALL t0.c0 FROM t0 WHERE (((0.654013919354451) BETWEEN SYMMETRIC (t0.c1) AND ("length"(upper(''))))OR(t0.c0));
	-- NULL
	-- NULL
	-- NULL
SELECT t0.c0 FROM t0 WHERE 1 BETWEEN SYMMETRIC t0.c1 AND 0;
	-- NULL
SELECT t0.c0 FROM t0 WHERE 1 BETWEEN t0.c1 AND 0;
	-- empty
SELECT ALL t0.c0 FROM t0 WHERE (((0.654013919354451) BETWEEN SYMMETRIC (t0.c1) AND ("length"(upper(''))))OR(t0.c0)) UNION ALL 
SELECT ALL t0.c0 FROM t0 WHERE NOT ((((0.654013919354451) BETWEEN SYMMETRIC (t0.c1) AND ("length"(upper(''))))OR(t0.c0))) UNION ALL 
SELECT t0.c0 FROM t0 WHERE ((((0.654013919354451) BETWEEN SYMMETRIC (t0.c1) AND ("length"(upper(''))))OR(t0.c0))) IS NULL;
	-- NULL
	-- NULL
	-- NULL
	-- False
ROLLBACK;

START TRANSACTION;
CREATE TABLE integers(i INTEGER);
INSERT INTO integers VALUES (1), (2), (3), (NULL);
select i between symmetric cast(1 as decimal) and cast(2 as double) from integers;
	-- True
	-- True
	-- False
	-- NULL
ROLLBACK;

START TRANSACTION; -- Bug 6910
CREATE TABLE t0("c0" DOUBLE NOT NULL);
COPY 11 RECORDS INTO "sys"."t0" FROM stdin USING DELIMITERS E'\t',E'\n','"';
0
0.9440456550171384
0.3985168253176739
0.9033732171086648
0.969477015070214
-4798112
0.6321209051017095
0.7740272412444077
0.7778437505533593
0.34293653568934335
0.1228907839970418

SELECT AVG(ALL ((((CAST(((+ (-266455613))/(sql_min(-384923592, -684635832))) AS INT))+(abs(((1380859174)*(1039801791))))))*(((((greatest(1447838997, -1100245247))/(((((849535137)||(-1532296761)))/(CAST(t0.c0 AS INT))))))*(- (+ (((-291947972)*(626914646))))))))), 0.77785585904529674916574322196538560092449188232421875 FROM t0 WHERE (0.06276420221300283) NOT  BETWEEN ASYMMETRIC (t0.c0) AND (t0.c0) 
GROUP BY 0.135845384683200176567652306403033435344696044921875, 0.86688447, 0.3299475775341662586725988148828037083148956298828125, + (+ (- (length(r'q')))), greatest(upper(upper(r'623865997')), trim(replace(r'vAd(Lup}Z', r'-1774953973', r'0.4235712099498189'), substr(r'', -1963236744)));
ROLLBACK;

START TRANSACTION; -- Bug 6911
CREATE TABLE "sys"."t0" ("c0" DECIMAL(18,3) NOT NULL);
COPY 41 RECORDS INTO "sys"."t0" FROM stdin USING DELIMITERS E'\t',E'\n','"';
0.388
0.371
0.939
0.790
0.493
0.148
0.841
0.034
0.189
0.087
0.048
0.227
0.759
0.446
0.622
0.579
0.453
0.819
0.768
0.027
0.839
0.506
0.560
0.362
0.453
0.728
0.078
0.364
0.559
0.829
0.061
0.854
0.920
0.440
0.627
0.838
0.113
0.501
0.283
0.722
0.819

-- calc.between a bulk operator missing
select t0.c0 from t0 where ((((cast(0.8800821070828266368124559448915533721446990966796875 as string))like(cast(greatest(r'0.29269552476776495', r'Aṵ\tmmz\\v/p*Q2Q5') as string(667)))))and((0.7706295839241474876502024926594458520412445068359375) 
between asymmetric (t0.c0) and (t0.c0))) union all
select all t0.c0 from t0 where not (((((cast(0.8800821070828266368124559448915533721446990966796875 as string))like(cast(greatest(r'0.29269552476776495', r'Aṵ\tmmz\\v/p*Q2Q5') as string(667)))))and((0.7706295839241474876502024926594458520412445068359375) 
between asymmetric (t0.c0) and (t0.c0)))) union all
select all t0.c0 from t0 where (((((cast(0.8800821070828266368124559448915533721446990966796875 as string))like(cast(greatest(r'0.29269552476776495', r'Aṵ\tmmz\\v/p*Q2Q5') as string(667)))))and((0.7706295839241474876502024926594458520412445068359375)
between asymmetric (t0.c0) and (t0.c0)))) is null;

SELECT ALL ((- (- (VAR_POP(ALL ((CAST(CAST(abs(709845242) AS INT) AS INT))%(abs(CAST((931144491) NOT IN (-587881807) AS INT))))))))^(- (+ (- (char_length(r'>])')))))), 0.5186927954967299, CAST("locate"(lower(r'[]'), rtrim(r'..ۄUH*6鉡q'), ((((-233289450)&(-476676450)))-(- (236876413)))) AS INT)
FROM t0 WHERE FALSE GROUP BY 1.2807753E7 HAVING MAX(ALL ((t0.c0)>=(NULL)));
ROLLBACK;
