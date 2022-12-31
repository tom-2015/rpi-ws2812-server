#ifndef FIFO_BUFFER
    #define FIFO_BUFFER

    #include <string.h>
    //https://embeddedartistry.com/blog/2017/05/17/creating-a-circular-buffer-in-c-and-c/
    //https://github.com/cgaebel/pipe
    
    struct fifo_buf_t {
        void * buffer;
        size_t head;
        size_t tail;
        size_t max; //of the buffer
        bool full;
        bool blocking;
    };


    void create_buffer(fifo_buf_t * fifo, size_t item_size, size_t count, bool blocking);

    size_t read_buffer(fifo_buf_t * fifo, void * destination, size_t max_len);

    size_t write_buffer(fifo_buf_t * fifo, void * source, size_t len);



#endif