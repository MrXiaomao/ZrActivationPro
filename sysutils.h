#ifndef SYSUTILS_H
#define SYSUTILS_H

class SysUtils
{
public:
    SysUtils();

    // 滑动平均滤波函数
    static void smooth(double *data, double *output, int data_size, int window_size);
};

#endif // SYSUTILS_H
