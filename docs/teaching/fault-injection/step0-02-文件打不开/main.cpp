// =============================================================================
//  step0 故障注入练习（坏版本 #2）：读取相对路径下的数据文件。
//  构建/安装/运行方法见同目录 README.md。
//  现象与工作表：docs/teaching/step0-cpp-骨架/fault-injection.md
// =============================================================================
#include <cstdio>

int main()
{
    // 约定：运行时从「当前工作目录」找 ./model/data.txt
    FILE *f = fopen("./model/data.txt", "r");
    if (!f)
    {
        printf("Open ./model/data.txt fail!\n");
        return 1;
    }

    char buf[128] = {0};
    if (fgets(buf, sizeof buf, f))
        printf("data: %s", buf);

    fclose(f);
    return 0;
}
