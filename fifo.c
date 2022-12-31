    #include "fifo.h"

    size_t fifo_count (fifo_buf_t * fifo){
        if (fifo->head >= fifo->tail){
            return fifo->head - fifo->tail;
        }else{
            return fifo->max - fifo->tail + fifo->head;
        }
    }

    size_t fifo_available(fifo_buf_t * fifo){
        return fifo->max - fifo_count(fifo);
    }

    size_t write_buffer(fifo_buf_t * fifo, void * source, size_t len){
        size_t size_to_end = fifo->max - fifo->head; //standard buffer is free until the end
        if (fifo->tail >= fifo->head) size_to_end = fifo->tail - fifo->head; //check if reading is behind writing, buffer is only free until write pointer

        if (len <= size_to_end){ //check if we can write in 1 time and buffer is free
            memcpy(&fifo->buffer[fifo->head], source, len);
            fifo->head +=len;
            if (len == size_to_end) fifo->head=0;
            if (fifo->tail==fifo->head) fifo->full=true;
            return len;
        }else{
            //copy to end of buffer
            memcpy(&fifo->buffer[fifo->head], source, size_to_end);
            source+=size_to_end;
            len -= size_to_end;
            size_t available = fifo->tail;
            if (available > len) {
                available = len;
            }else if (available == len){
                fifo->full = true;
            }else{
                
            }
            memcpy(fifo->buffer, source, available);
            fifo->head = available;
            return size_to_end + available;
        }
    }

    size_t read_buffer(fifo_buf_t * fifo, void * destination, size_t max_len){
        size_t end = fifo->head; //first find the end of the buffer (max or head)
        if (fifo->full || fifo->head < fifo->tail) end = fifo->max; //head is beyond end

        size_t read_bytes = end - fifo->tail; //we can read this ammount of bytes from buffer

        if (read_bytes > 0){
            if (read_bytes > max_len) read_bytes = max_len; //check if we can store that ammount
            memcpy(destination, &fifo->buffer[fifo->tail], read_bytes);

            fifo->tail+=read_bytes;
            if (fifo->tail>=fifo->max) fifo->tail = 0;
            fifo->full=false;

            //check if destination buffer is not full and more data available in the beginning of the buffer
            if (read_bytes < max_len && fifo->head > fifo->tail){
                size_t read_more = fifo->head; //we will read more bytes from beginning of buffer
                if (read_more + read_bytes > max_len) read_more = max_len - read_bytes; //check if destination buffer is large enough to read all available data
                memcpy(&destination[read_bytes], fifo->buffer, read_more); //copy
                fifo->tail+=read_more;
                return read_bytes + read_more; //return total bytes read
            }
        }

        return read_bytes; //return total bytes read

       /* if (fifo->head > fifo->tail){
            available = fifo->head - fifo->tail;
            if (available > max_len) available = max_len;
            memcpy(destination, &fifo->buffer[fifo->tail], available);
            
            fifo->tail +=available;
            if (fifo->tail > fifo->max) fifo->tail=0;

        }else{
            available = fifo->max - fifo->tail + fifo->head;
            memcpy(destination, &fifo->buffer[fifo->tail], available);


        }
        



        size_t read = min (fifo_available(fifo), max_len);
        memcpy(destination, fifo->buffer, read);
        return read;*/
    }
