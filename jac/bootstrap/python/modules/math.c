/* Public math signatures and module constants; algorithms live in native Jac. */
#include <Python.h>
#include <stdint.h>
#define H(p) ((uint64_t)(uintptr_t)(p))
#define O(h) ((PyObject *)(uintptr_t)(h))
extern uint64_t jacpy_math_factorial(uint64_t), jacpy_math_isqrt(uint64_t);
extern uint64_t jacpy_math_gcd(uint64_t, int64_t), jacpy_math_comb(uint64_t, uint64_t, int64_t);
static PyObject *factorial(PyObject *module, PyObject *value) { return O(jacpy_math_factorial(H(value))); }
static PyObject *isqrt(PyObject *module, PyObject *value) { return O(jacpy_math_isqrt(H(value))); }
static PyObject *gcd(PyObject *module, PyObject *args) { return O(jacpy_math_gcd(H(args),0)); }
static PyObject *lcm(PyObject *module, PyObject *args) { return O(jacpy_math_gcd(H(args),1)); }
static PyObject *comb(PyObject *module, PyObject *args) {
    PyObject *n,*k; if(!PyArg_ParseTuple(args,"OO:comb",&n,&k)) return NULL;
    return O(jacpy_math_comb(H(n),H(k),1));
}
static PyObject *perm(PyObject *module, PyObject *args) {
    PyObject *n,*k=Py_None; if(!PyArg_ParseTuple(args,"O|O:perm",&n,&k)) return NULL;
    return O(jacpy_math_comb(H(n),H(k),0));
}
extern uint64_t jacpy_math_unary(int64_t, uint64_t), jacpy_math_binary(int64_t,uint64_t,uint64_t);
extern uint64_t jacpy_math_log(uint64_t,uint64_t), jacpy_math_parts(int64_t,uint64_t), jacpy_math_ldexp(uint64_t,uint64_t);
extern uint64_t jacpy_math_fma(uint64_t,uint64_t,uint64_t), jacpy_math_isclose(uint64_t,uint64_t,uint64_t,uint64_t), jacpy_math_nextafter(uint64_t,uint64_t,uint64_t);
#define UNARY(name, code) static PyObject *wrap_##name(PyObject *module, PyObject *value) { return O(jacpy_math_unary(code,H(value))); }
#define BINARY(name, code) static PyObject *wrap_##name(PyObject *module, PyObject *args) { PyObject *a,*b; if(!PyArg_ParseTuple(args,"OO:" #name,&a,&b)) return NULL; return O(jacpy_math_binary(code,H(a),H(b))); }
UNARY(acos, 0)
UNARY(acosh, 1)
UNARY(asin, 2)
UNARY(asinh, 3)
UNARY(atan, 4)
UNARY(atanh, 5)
UNARY(cbrt, 6)
UNARY(cos, 7)
UNARY(cosh, 8)
UNARY(erf, 9)
UNARY(erfc, 10)
UNARY(exp, 11)
UNARY(exp2, 12)
UNARY(expm1, 13)
UNARY(fabs, 14)
UNARY(gamma, 15)
UNARY(lgamma, 16)
UNARY(log1p, 17)
UNARY(sin, 18)
UNARY(sinh, 19)
UNARY(sqrt, 20)
UNARY(tan, 21)
UNARY(tanh, 22)
UNARY(degrees, 23)
UNARY(radians, 24)
UNARY(isfinite, 25)
UNARY(isinf, 26)
UNARY(isnan, 27)
UNARY(ceil, 28)
UNARY(floor, 29)
UNARY(trunc, 30)
UNARY(log10, 31)
UNARY(log2, 32)
UNARY(ulp, 33)
BINARY(atan2, 0)
BINARY(copysign, 1)
BINARY(fmod, 2)
BINARY(remainder, 3)
BINARY(pow, 4)

static PyObject *wrap_log(PyObject *module, PyObject *args) { PyObject *value,*base=NULL; if(!PyArg_ParseTuple(args,"O|O:log",&value,&base)) return NULL; return O(jacpy_math_log(H(value),H(base))); }
static PyObject *wrap_frexp(PyObject *module, PyObject *value) { return O(jacpy_math_parts(0,H(value))); }
static PyObject *wrap_modf(PyObject *module, PyObject *value) { return O(jacpy_math_parts(1,H(value))); }
static PyObject *wrap_ldexp(PyObject *module, PyObject *args) { PyObject *value,*exponent; if(!PyArg_ParseTuple(args,"OO:ldexp",&value,&exponent)) return NULL; return O(jacpy_math_ldexp(H(value),H(exponent))); }
static PyObject *wrap_fma(PyObject *module, PyObject *args) { PyObject *a,*b,*c; if(!PyArg_ParseTuple(args,"OOO:fma",&a,&b,&c)) return NULL; return O(jacpy_math_fma(H(a),H(b),H(c))); }
static PyObject *wrap_isclose(PyObject *module, PyObject *args, PyObject *kw) { static char *names[]={"a","b","rel_tol","abs_tol",NULL}; PyObject *a,*b,*rel=NULL,*abs=NULL; if(!PyArg_ParseTupleAndKeywords(args,kw,"OO|$OO:isclose",names,&a,&b,&rel,&abs)) return NULL; return O(jacpy_math_isclose(H(a),H(b),H(rel),H(abs))); }
static PyObject *wrap_nextafter(PyObject *module, PyObject *args, PyObject *kw) { static char *names[]={"","","steps",NULL}; PyObject *a,*b,*steps=NULL; if(!PyArg_ParseTupleAndKeywords(args,kw,"OO|$O:nextafter",names,&a,&b,&steps)) return NULL; return O(jacpy_math_nextafter(H(a),H(b),H(steps))); }
extern uint64_t jacpy_math_fsum(uint64_t), jacpy_math_hypot(uint64_t), jacpy_math_dist(uint64_t,uint64_t), jacpy_math_prod(uint64_t,uint64_t), jacpy_math_sumprod(uint64_t,uint64_t);
static PyObject *wrap_fsum(PyObject *module, PyObject *value) { return O(jacpy_math_fsum(H(value))); }
static PyObject *wrap_hypot(PyObject *module, PyObject *args) { return O(jacpy_math_hypot(H(args))); }
static PyObject *wrap_dist(PyObject *module, PyObject *args) { PyObject *a,*b; if(!PyArg_ParseTuple(args,"OO:dist",&a,&b)) return NULL; return O(jacpy_math_dist(H(a),H(b))); }
static PyObject *wrap_sumprod(PyObject *module, PyObject *args) { PyObject *a,*b; if(!PyArg_ParseTuple(args,"OO:sumprod",&a,&b)) return NULL; return O(jacpy_math_sumprod(H(a),H(b))); }
static PyObject *wrap_prod(PyObject *module, PyObject *args, PyObject *kw) { static char *names[]={"","start",NULL}; PyObject *value,*start=NULL; if(!PyArg_ParseTupleAndKeywords(args,kw,"O|$O:prod",names,&value,&start)) return NULL; return O(jacpy_math_prod(H(value),H(start))); }
static PyMethodDef methods[]={
    {"factorial",factorial,METH_O,"factorial($module, n, /)\n--\n\nReturn n factorial."},
    {"isqrt",isqrt,METH_O,"isqrt($module, n, /)\n--\n\nReturn the integer part of the square root of n."},
    {"gcd",gcd,METH_VARARGS,"gcd($module, *integers)\n--\n\nGreatest common divisor."},
    {"lcm",lcm,METH_VARARGS,"lcm($module, *integers)\n--\n\nLeast common multiple."},
    {"comb",comb,METH_VARARGS,"comb($module, n, k, /)\n--\n\nNumber of unordered selections without repetition."},
    {"perm",perm,METH_VARARGS,"perm($module, n, k=None, /)\n--\n\nNumber of ordered selections without repetition."},
    {"acos",wrap_acos,METH_O,"Return the arc cosine (measured in radians) of x.\n\nThe result is between 0 and pi."},
    {"acosh",wrap_acosh,METH_O,"Return the inverse hyperbolic cosine of x."},
    {"asin",wrap_asin,METH_O,"Return the arc sine (measured in radians) of x.\n\nThe result is between -pi/2 and pi/2."},
    {"asinh",wrap_asinh,METH_O,"Return the inverse hyperbolic sine of x."},
    {"atan",wrap_atan,METH_O,"Return the arc tangent (measured in radians) of x.\n\nThe result is between -pi/2 and pi/2."},
    {"atanh",wrap_atanh,METH_O,"Return the inverse hyperbolic tangent of x."},
    {"cbrt",wrap_cbrt,METH_O,"Return the cube root of x."},
    {"cos",wrap_cos,METH_O,"Return the cosine of x (measured in radians)."},
    {"cosh",wrap_cosh,METH_O,"Return the hyperbolic cosine of x."},
    {"erf",wrap_erf,METH_O,"Error function at x."},
    {"erfc",wrap_erfc,METH_O,"Complementary error function at x."},
    {"exp",wrap_exp,METH_O,"Return e raised to the power of x."},
    {"exp2",wrap_exp2,METH_O,"Return 2 raised to the power of x."},
    {"expm1",wrap_expm1,METH_O,"Return exp(x)-1.\n\nThis function avoids the loss of precision involved in the direct evaluation of exp(x)-1 for small x."},
    {"fabs",wrap_fabs,METH_O,"Return the absolute value of the float x."},
    {"gamma",wrap_gamma,METH_O,"Gamma function at x."},
    {"lgamma",wrap_lgamma,METH_O,"Natural logarithm of absolute value of Gamma function at x."},
    {"log1p",wrap_log1p,METH_O,"Return the natural logarithm of 1+x (base e).\n\nThe result is computed in a way which is accurate for x near zero."},
    {"sin",wrap_sin,METH_O,"Return the sine of x (measured in radians)."},
    {"sinh",wrap_sinh,METH_O,"Return the hyperbolic sine of x."},
    {"sqrt",wrap_sqrt,METH_O,"Return the square root of x."},
    {"tan",wrap_tan,METH_O,"Return the tangent of x (measured in radians)."},
    {"tanh",wrap_tanh,METH_O,"Return the hyperbolic tangent of x."},
    {"degrees",wrap_degrees,METH_O,"Convert angle x from radians to degrees."},
    {"radians",wrap_radians,METH_O,"Convert angle x from degrees to radians."},
    {"isfinite",wrap_isfinite,METH_O,"Return True if x is neither an infinity nor a NaN, and False otherwise."},
    {"isinf",wrap_isinf,METH_O,"Return True if x is a positive or negative infinity, and False otherwise."},
    {"isnan",wrap_isnan,METH_O,"Return True if x is a NaN (not a number), and False otherwise."},
    {"ceil",wrap_ceil,METH_O,"Return the ceiling of x as an Integral.\n\nThis is the smallest integer >= x."},
    {"floor",wrap_floor,METH_O,"Return the floor of x as an Integral.\n\nThis is the largest integer <= x."},
    {"trunc",wrap_trunc,METH_O,"Truncates the Real x to the nearest Integral toward 0.\n\nUses the __trunc__ magic method."},
    {"log10",wrap_log10,METH_O,"Return the base 10 logarithm of x."},
    {"log2",wrap_log2,METH_O,"Return the base 2 logarithm of x."},
    {"ulp",wrap_ulp,METH_O,"Return the value of the least significant bit of the float x."},
    {"atan2",wrap_atan2,METH_VARARGS,"Return the arc tangent (measured in radians) of y/x.\n\nUnlike atan(y/x), the signs of both x and y are considered."},
    {"copysign",wrap_copysign,METH_VARARGS,"Return a float with the magnitude (absolute value) of x but the sign of y.\n\nOn platforms that support signed zeros, copysign(1.0, -0.0)\nreturns -1.0.\n"},
    {"fmod",wrap_fmod,METH_VARARGS,"Return fmod(x, y), according to platform C.\n\nx % y may differ."},
    {"remainder",wrap_remainder,METH_VARARGS,"Difference between x and the closest integer multiple of y.\n\nReturn x - n*y where n*y is the closest integer multiple of y.\nIn the case where x is exactly halfway between two multiples of\ny, the nearest even value of n is used. The result is always exact."},
    {"pow",wrap_pow,METH_VARARGS,"Return x**y (x to the power of y)."},
    {"log",wrap_log,METH_VARARGS,"log(x, [base=math.e])\nReturn the logarithm of x to the given base.\n\nIf the base is not specified, returns the natural logarithm (base e) of x."},
    {"ldexp",wrap_ldexp,METH_VARARGS,"Return x * (2**i).\n\nThis is essentially the inverse of frexp()."},
    {"fma",wrap_fma,METH_VARARGS,"Fused multiply-add operation.\n\nCompute (x * y) + z with a single round."},
    {"frexp",wrap_frexp,METH_O,"Return the mantissa and exponent of x, as pair (m, e).\n\nIf x is a finite nonzero number, then m is a float with\n0.5 <= abs(m) < 1.0 and an integer e is such that\nx == m * 2**e exactly.  Else, return (x, 0)."},{"modf",wrap_modf,METH_O,"Return the fractional and integer parts of x.\n\nBoth results carry the sign of x and are floats."},
    {"isclose",(PyCFunction)(void(*)(void))wrap_isclose,METH_VARARGS|METH_KEYWORDS,"Determine whether two floating-point numbers are close in value.\n\n  rel_tol\n    maximum difference for being considered \"close\", relative to the\n    magnitude of the input values\n  abs_tol\n    maximum difference for being considered \"close\", regardless of the\n    magnitude of the input values\n\nReturn True if a is close in value to b, and False otherwise.\n\nFor the values to be considered close, the difference between them\nmust be smaller than at least one of the tolerances.\n\n-inf, inf and NaN behave similarly to the IEEE 754 Standard.  That\nis, NaN is not close to anything, even itself.  inf and -inf are\nonly close to themselves."},
    {"nextafter",(PyCFunction)(void(*)(void))wrap_nextafter,METH_VARARGS|METH_KEYWORDS,"Return the floating-point value the given number of steps after x towards y.\n\nIf steps is not specified or is None, it defaults to 1.\n\nRaises a TypeError, if x or y is not a double, or if steps is not\nan integer.  Raises ValueError if steps is negative."},
    {"fsum",wrap_fsum,METH_O,"Return an accurate floating-point sum of values in the iterable seq.\n\nAssumes IEEE-754 floating-point arithmetic."},{"hypot",wrap_hypot,METH_VARARGS,"Multidimensional Euclidean distance from the origin to a point.\n\nRoughly equivalent to:\n    sqrt(sum(x**2 for x in coordinates))\n\nFor a two dimensional point (x, y), gives the hypotenuse\nusing the Pythagorean theorem:  sqrt(x*x + y*y).\n\nFor example, the hypotenuse of a 3/4/5 right triangle is:\n\n    >>> hypot(3.0, 4.0)\n    5.0"},{"dist",wrap_dist,METH_VARARGS,"Return the Euclidean distance between two points p and q.\n\nThe points should be specified as sequences (or iterables) of\ncoordinates.  Both inputs must have the same dimension.\n\nRoughly equivalent to:\n    sqrt(sum((px - qx) ** 2.0 for px, qx in zip(p, q)))"},{"sumprod",wrap_sumprod,METH_VARARGS,"Return the sum of products of values from two iterables p and q.\n\nRoughly equivalent to:\n\n    sum(map(operator.mul, p, q, strict=True))\n\nFor float and mixed int/float inputs, the intermediate products\nand sums are computed with extended precision."},
    {"prod",(PyCFunction)(void(*)(void))wrap_prod,METH_VARARGS|METH_KEYWORDS,"Calculate the product of all the elements in the input iterable.\n\nThe default start value for the product is 1.\n\nWhen the iterable is empty, return the start value.  This function is\nintended specifically for use with numeric values and may reject\nnon-numeric types."},
    {NULL}
};
static int constant(PyObject *module, const char *name, double value) { PyObject *object=PyFloat_FromDouble(value); if(!object) return -1; int result=PyModule_AddObjectRef(module,name,object); Py_DECREF(object); return result; }
static int module_exec(PyObject *module) {
    return constant(module,"pi",3.14159265358979323846)<0 || constant(module,"e",2.71828182845904523536)<0 || constant(module,"tau",6.28318530717958647692)<0 || constant(module,"inf",Py_INFINITY)<0 || constant(module,"nan",Py_NAN)<0 ? -1 : 0;
}
static PyModuleDef_Slot slots[]={{Py_mod_exec,module_exec},{Py_mod_multiple_interpreters,Py_MOD_PER_INTERPRETER_GIL_SUPPORTED},{0,NULL}};
static PyModuleDef definition={PyModuleDef_HEAD_INIT,"math","Native Jac mathematics.",0,methods,slots,NULL,NULL,NULL};
PyMODINIT_FUNC PyInit_math(void) { return PyModuleDef_Init(&definition); }
