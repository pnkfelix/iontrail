var buffer = new Array(10);
for(var i=0; i < buffer.length; i++) buffer[i] = undefined;
var d1 =  new ParallelMatrixDebt([5], buffer, 0);
var d2 =  new ParallelMatrixDebt([5], buffer, 5);
var pm1 =  new ParallelMatrix(d1, function (i,j) { print("callback for pm1 i:"+i+" j:"+j); j = j ? j : 0; return 100+i*10+j; });
var pm2 =  new ParallelMatrix(d2, function (i,j) { print("callback for pm1 i:"+i+" j:"+j); j = j ? j : 0; return 200+i*10+j; });
