#include <stdio.h>
#include "rkYolov5s.hpp"

int main(int argc, char **argv)
{ 
    if (argc != 2)
    {
        printf("Usage: %s <model_path>\n", argv[0]);
        return -1;
    }

    rkYolov5s model(argv[1]);
    if (model.init(nullptr, false) != 0)
    {
        printf("model init failed\n");
        return -1;
    }

    printf("B1 OK\n");
    return 0; 

}
