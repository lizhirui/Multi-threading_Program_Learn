#include <cstdio>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <thread>
#include <ctime>

using namespace std;

template <typename T>
class lock_free_queue
{
    private:
        const int32_t empty = 0;
        const int32_t writing = 1;
        const int32_t valid = 2;
        const int32_t reading = 3;

        struct node
        {
            atomic_int32_t status;
            T value;
        };

        struct node *_items;
        atomic_int32_t _head;
        atomic_int32_t _tail;
        int _size;
        int _sizebit;
        int _sizebitmask;

        int get_index(int x)
        {
            return x & _sizebitmask;
        }

    public:
        lock_free_queue(int size)
        {
            _size = size;
            _sizebit = (sizeof(size) << 3) - __builtin_clz(size) - 1;

            if(size & (size - 1))
            {
                _sizebit++;
                _size = 1 << _sizebit;
            }

            _sizebitmask = _size - 1;
            _items = new struct node[_size];

            for(int i = 0;i < _size;i++)
            {
                _items[i].status = empty;
            }

            _head = 0;
            _tail = 0;
        }

        ~lock_free_queue()
        {
            delete[] _items;
        }

        bool is_empty()
        {
            return _head.load() == _tail.load();
        }

        bool is_full()
        {
            return _head.load() == get_index(_tail.load() + 1);
        }

        bool enqueue(T element,int start = 0)
        {
            int tail = 0;
            auto v_empty = empty;
            auto v_writing = writing;

            do{
                do
                {
                    tail = _tail.load();

                    if(is_full())
                    {
                        return false;
                    }
                    
                    v_empty = empty;
                }while(!_items[tail].status.compare_exchange_strong(v_empty,writing));

                if(tail == _tail.load())
                {
                    break;
                }

                v_writing = writing;
                assert(_items[tail].status.compare_exchange_strong(v_writing,empty));
            }while(1);

            _items[tail].value = element;
            v_writing = writing;
            assert(_items[tail].status.compare_exchange_strong(v_writing,valid));
            assert(_tail.compare_exchange_strong(tail,get_index(tail + 1)));
            return true;
        }

        bool dequeue(T &element)
        {
            int head = 0;
            auto v_valid = valid;
            auto v_reading = reading;
            
            do
            { 
                do
                {
                    head = _head.load();

                    if(is_empty())
                    {
                        return false;
                    }

                    v_valid = valid;
                }while(!_items[head].status.compare_exchange_strong(v_valid,reading));

                if(head == _head.load())
                {
                    break;
                }

                v_reading = reading;
                assert(_items[head].status.compare_exchange_strong(v_reading,valid));
            }while(1);

            element = _items[head].value;
            v_reading = reading;
            assert(_items[head].status.compare_exchange_strong(v_reading,empty));
            assert(_head.compare_exchange_strong(head,get_index(head + 1)));
            return true;
        }
};

#define PRO_THREAD_NUM 10
#define CUS_THREAD_NUM 10
#define MAX_NUM 1000000

#define TASK_SIZE (MAX_NUM / PRO_THREAD_NUM)

#if (MAX_NUM % PRO_THREAD_NUM) != 0
    #error "MAX_NUM % PRO_THREAD_NUM isn't 0"
#endif

thread *pro_thread[PRO_THREAD_NUM];
thread *cus_thread[CUS_THREAD_NUM];
bool visited[MAX_NUM];

lock_free_queue<int> q(8);
atomic_int32_t producer_finish_num(0);

void thread_producer(int start,int end)
{
    for(int i = start;i <= end;i++)
    {
        while(!q.enqueue(i,start));
    }

    producer_finish_num.fetch_add(1);
}

void thread_customer()
{
    while((producer_finish_num < PRO_THREAD_NUM) || (!q.is_empty()))
    {
        int x;

        if(q.dequeue(x))
        {
            visited[x] = true;
        }
    }
}

int main()
{
    int x = 7;
    int y = 0;
    lock_free_queue<int> q(5);

    bool ret1 = q.enqueue(x);
    bool ret2 = q.dequeue(y);
    printf("ret1 = %d,ret2 = %d,y = %d\n",ret1,ret2,y);
    
    assert(MAX_NUM % PRO_THREAD_NUM == 0);

    printf("starting threading...\n");
    printf("PRO_THREAD_NUM = %d,CUS_THREAD_NUM = %d,TASK_SIZE = %d,MAX_NUM = %d\n",PRO_THREAD_NUM,CUS_THREAD_NUM,TASK_SIZE,MAX_NUM);

    for(int i = 0;i < MAX_NUM;i++)
    {
        visited[i] = false;
    }

    clock_t start_time = clock();

    for(int i = 0;i < min(PRO_THREAD_NUM,CUS_THREAD_NUM);i++)
    {
        if(i % 1)
        {
            pro_thread[i] = new thread(thread_producer,i * TASK_SIZE,(i + 1) * TASK_SIZE - 1);
            cus_thread[i] = new thread(thread_customer);
        }
        else
        {
            cus_thread[i] = new thread(thread_customer);
            pro_thread[i] = new thread(thread_producer,i * TASK_SIZE,(i + 1) * TASK_SIZE - 1);
        }
    }

    #if PRO_THREAD_NUM <= CUS_THREAD_NUM

        for(int i = PRO_THREAD_NUM;i < CUS_THREAD_NUM;i++)
        {
            cus_thread[i] = new thread(thread_customer);
        }

    #else

        for(int i = CUS_THREAD_NUM;i < PRO_THREAD_NUM;i++)
        {
            pro_thread[i] = new thread(thread_producer,i * TASK_SIZE,(i + 1) * TASK_SIZE - 1);
        }

    #endif

    for(int i = 0;i < PRO_THREAD_NUM;i++)
    {
        pro_thread[i] -> join();
    }

    printf("pro_thread ok\n");

    for(int i = 0;i < CUS_THREAD_NUM;i++)
    {
        cus_thread[i] -> join();
    }

    clock_t end_time = clock();

    bool error = false;

    for(int i = 0;i < MAX_NUM;i++)
    {
        if(!visited[i])
        {
            //printf("i = %d\n",i);
            error = true;
        }
    }

    printf("error = %d,run time is %.4lf ms\n",error,((double)(end_time - start_time)) * 1000.0 / CLOCKS_PER_SEC);
    return 0;
}