#include "sysutils.h"

#include <stdio.h>

SysUtils::SysUtils() {}

// 滑动平均滤波函数
void SysUtils::smooth(double *data, double *output, int data_size, int window_size) {
    // 如果窗口大小大于数据长度，则不进行滤波
    if (window_size > data_size) {
        printf("窗口大小不能大于数据大小。\n");
        return;
    }

    // 窗口宽度必须是奇数
    if ( window_size%2 == 0) {
        printf("smooth,窗口宽度只能是奇数，当前窗口宽度%d。\n", window_size);
        return;
    }

    int halfWindow = (window_size - 1) / 2;

    // 计算每个点的滑动平均值
    for (int i = 0; i < data_size; i++) {
        double sum = 0.0;
        int count = 0;
        // 计算窗口内的平均值
        for (int j = i - (window_size - 1) / 2; j <= i + (window_size - 1) / 2; j++) {
            // 确保索引在有效范围内
            if (j >= 0 && j < data_size) {
                sum += data[j];
                count++;
            }
        }
        // 计算并存储当前位置的滑动平均值
        output[i] = sum / count;
    }

    // 重新处理左边界情况
    for (int i = 0; i < halfWindow; i++) {
        double sum = 0.0;
        int count = 0;
        //给出左侧端点个数
        int leftPoint = i;
        for (int j = 0; j <= i + i; j++) {
            // printf("Left side, i = %d, data[%d]=%f\n", i, j, data[j]);
            sum += data[j];
            count++;
        }
        output[i] = sum / count;
    }

    // 重新处理右边界情况
    for (int i = data_size - halfWindow; i < data_size; i++) {
        double sum = 0.0;
        int count = 0;
        //给出右侧端点个数
        int leftPoint = data_size - i- 1;
        for (int j = i - leftPoint; j < data_size; j++) {
            // printf("Right side, i = %d, data[%d]=%f\n", i, j, data[j]);
            sum += data[j];
            count++;
        }
        output[i] = sum / count;
    }
}
