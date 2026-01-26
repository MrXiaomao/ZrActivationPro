#ifndef SYSUTILS_H
#define SYSUTILS_H
#include <vector>
class SysUtils
{
public:
    SysUtils();

    // 滑动平均滤波函数
    static void smooth(double *data, double *output, int data_size, int window_size);

    // MATLAB-like sgolayfilt for 1D data, smoothing only (derivative order d = 0)
    // p: polynomial order, f: frame length (odd)
    static std::vector<double> sgolayfilt_matlab_like(const std::vector<double>& x,int p,int f);
};

#endif // SYSUTILS_H
