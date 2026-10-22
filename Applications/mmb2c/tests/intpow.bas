' Integer ^ integer as op_exp (Operators.c): an integer by squaring,
' wrapping at 64 bits; a negative exponent gives pow()'s float.
Print "fold "; 2^10; 10^6; 2^3^2; (-2)^3; -2^2; 0^0
Print "exact "; 2^62; 3^39; 7^20
Print "wrap "; 2^63; 2^64; 3^41
Print "neg literal "; 2^-1
n% = -3
x = 10^n%
k% = 2^(n% + 2)
Print "neg runtime "; 10^n%; x; k%
n% = 5
y = 2^n% / 4
Print "runtime "; 2^n%; y; 3^n% - 1
Print "float "; 1.5^2; 2^0.5
Dim a(2^4)
Print "dim "; Bound(a(), 1)
