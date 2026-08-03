for f in autoinit blocktypedef braceelide dbl escapes fileio fmt fp goto libtest ll2 mainret mixdecl namespace optest ptrarray rpn scope sieve statics strs struct2 struct3 struct4 sw2 tagscope unaryplus voidcomma width3
do
echo "=== $f"
rm -f $f.bc
cc -o $f.bc $f.c
./$f.bc
done
echo "=== END"
