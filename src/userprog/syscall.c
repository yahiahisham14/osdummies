#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/shutdown.h"
#include "threads/vaddr.h"
//#include "lib/user/syscall.h"


//typedef pid_t int;
typedef int pid_t;
#define PID_ERROR ((pid_t) -1)

static void syscall_handler (struct intr_frame *);

static void halt (void);
static void exit (int status);
static pid_t exec ( const char *cmd_line );
static int wait (pid_t pid);
static bool create (const char *file, unsigned initial_size);
static bool remove (const char *file);
static int open (const char *file);
static int filesize (int fd);
static int read (int fd, void *buffer, unsigned size);
static int write (int fd, const void *buffer, unsigned size);
static void seek (int fd, unsigned position);
static unsigned tell (int fd);
static void close (int fd);

static void kill_process ();

static struct lock sync_lock;

static void check(void *esp);
static void 
get_Args(void* esp , int args_count , void** arg_0, void ** arg_1 , void ** arg_2 );

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init ( &sync_lock );
}

/* Time line of the syscall_handler:
	1 - Fetch the stack ptr
	2 - Check that it's valid stack ptr --> validate that it's user stack ptr
	3 - Fetch the system call number (Top of stack)
	4 - Call the appropriate system call method
*/
static void
syscall_handler (struct intr_frame *f ) 
{
  
	/* 1 - Fetch Stack ptr */
	void *esp = f->esp;

	/* 2 - Validate the stack ptr */
	check(esp);

	/* 3 - Fetch system call number */
	int sys_call_number = (int) (*(int *)esp);
	

	//argument pointers.
	void *arg_0;
	void *arg_1;
	void *arg_2;

	esp += 4;
	check(esp);
	
	
	/* 4 - Call the appropriate system call method */
	switch(sys_call_number){
		case SYS_HALT:

			get_Args(esp  , 0 ,&arg_0 ,&arg_1 ,&arg_2);
			halt();

			break;
		case SYS_EXIT:

			get_Args(esp  , 1 ,&arg_0 ,&arg_1 ,&arg_2);
			exit( (int) arg_0 );

			break;
		case SYS_EXEC:

			get_Args(esp  , 1 ,&arg_0 ,&arg_1 ,&arg_2);
			f->eax = (uint32_t) exec( (char *) arg_0);

			break;
		case SYS_WAIT:

			get_Args(esp  , 1 ,&arg_0 ,&arg_1 ,&arg_2);
			f->eax = (uint32_t) wait( (pid_t) arg_0);

			break;
		case SYS_CREATE:

			get_Args(esp  , 2 ,&arg_0 ,&arg_1 ,&arg_2);
			f->eax = (uint32_t) create( (char *) arg_0, (unsigned) arg_1);

			break;		
		case SYS_REMOVE:

			get_Args(esp  , 1 ,&arg_0 ,&arg_1 ,&arg_2);
			f->eax = (uint32_t) remove( (char *) arg_0);

			break;
		case SYS_OPEN:

			get_Args(esp  , 1 ,&arg_0 ,&arg_1 ,&arg_2);
			f->eax = (uint32_t) open( (char *) arg_0);
			
			break;
		case SYS_FILESIZE:

			get_Args(esp  , 1 ,&arg_0 ,&arg_1 ,&arg_2);
			f->eax = (uint32_t) filesize( (int) arg_0 );

			break;
		case SYS_READ:

			get_Args(esp  , 3 ,&arg_0 ,&arg_1 ,&arg_2);
			
			f->eax = (uint32_t) read ( (int)arg_0, (void *) arg_1, (unsigned) arg_2 );

			break;
		case SYS_WRITE:

			get_Args(esp , 3 ,&arg_0 ,&arg_1 ,&arg_2);
			f->eax = (uint32_t) write ( (int) arg_0, (const void *) arg_1, (unsigned) arg_2 );

			break;
		case SYS_SEEK:

			get_Args(esp  , 2 ,&arg_0 ,&arg_1 ,&arg_2);
			seek ( (int) arg_0, (unsigned)arg_1 );

			break;
		case SYS_TELL:

			get_Args(esp  , 1 ,&arg_0 ,&arg_1 ,&arg_2);
			f->eax = (uint32_t) tell ( (int) arg_0 ) ;

			break;
		case SYS_CLOSE:

			get_Args(esp  , 1 ,&arg_0 ,&arg_1 ,&arg_2);
			close ( (int) arg_0 );

			break;
		default:
			kill_process ();
			break;
	}
	

}

static void
halt (void){
	shutdown_power_off ();
}//end function

static void
exit (int status)
{
	char* fileName = thread_current()->name;
	char temp[20];
	int i = 0;
	while( fileName[i] != ' ' && fileName[i] != NULL ){
		 temp[i] = fileName[i];
		i++;
	}//end while
	temp[i] = '\0';
	struct thread *cur = thread_current ();
  	cur->process_info->exit_status = status;
	printf("%s: exit(%d)\n",temp, status );
	thread_exit ();
}//end function.

static pid_t
exec ( const char *cmd_line )
{

  check(cmd_line);

  pid_t pid = (pid_t) process_execute (cmd_line);

  /* If pid allocation fails, exit -1 */
  if (pid == -1) 
    return -1;

  /* Wait to receive message about child loading success */
  struct thread* t = thread_current ();
  sema_down (&t->process_info->sema_load);

  if (t->process_info->child_load_success)
      return pid;
  else
      return -1;

}//end function

static int
wait (pid_t pid)
{
	return process_wait(pid);
}//end function.

static bool
create (const char *file, unsigned initial_size)
{
	check(file);
	// Aquire lock for acessing file system
	lock_acquire ( &sync_lock);

	// Call file system create
	bool created_succ = filesys_create (file, initial_size);

	// Release lock
	lock_release ( &sync_lock );

	return created_succ;
	
}//end function.

static bool
remove (const char *file)
{
	// Aquire lock for acessing file system
	lock_acquire ( &sync_lock);

	// Call file system create
	bool deleted_succ = filesys_remove (file);

	// Release lock
	lock_release ( &sync_lock );

	return deleted_succ;
	
}//end function.

static int
open (const char *file){

	// Check if name is null
	if(file == NULL) 
		return -1;

	// Check if file name is in user address space
	check(file);
	
	
	// Aquire lock for acessing file system
	lock_acquire ( &sync_lock);
	
	// Open file
	struct filesize * opened_file = filesys_open (file);

	// Release lock
	lock_release ( &sync_lock );

	if (opened_file == NULL){
		// Return fd error
		return -1;
	}

	struct thread* t = thread_current();
	struct file_map* new_file_map =
    (struct file_map *) malloc (sizeof (struct file_map));
     new_file_map->f = opened_file;

	int i;
	//i starts at 2 because 0 and  are reserved.
	for( i = 2 ; i < 130 ; i++){
		
		if(t->map[i] == NULL){

			//struct file_map* new_file_map = { opened_file ,0};
			t->map[i] = new_file_map;
			ASSERT( t->map[i] != NULL );
			//i is the value of fd.
			return i;
		}
	}

	//must not reached.
	ASSERT(0);

}//end function.

static int
filesize (int fd)
{
       if( fd == NULL || fd < 0 || fd > 130 ||thread_current()->map[fd] == NULL  ){
			kill_process();
		}
 
        struct thread * t = thread_current();
 
        // if(t->map[fd] == NULL)
        //         return -1;
 
        struct file * f = t->map[fd]->f;
 
        // Aquire lock for acessing file system
        lock_acquire ( &sync_lock);
 
        int size = file_length(f);
 		//printf("filesize : %d\n",size );
        // Release lock
        lock_release ( &sync_lock );
 
        return size;
 
}//end function.
 
static int
read (int fd, void *buffer, unsigned size)
{	

		check(buffer);

        if( fd == NULL || fd < 0 || fd > 129 ||thread_current()->map[fd] == NULL  ){
			kill_process();
		}
 	
        //fd =0 -> reads input from keyboard.
	     if (fd == 0)       /* Read from input */
	    {
	    	int result = 0;
	      unsigned i = 0;
	      for (i = 0; i < size; i++)
	        {
	          *(uint8_t *)buffer = input_getc();
	          result++;
	          buffer++;
	        }
	        return result;
	    }//end if.
 
        struct thread * t = thread_current();
 
        struct file * pf = t->map[fd]->f;
 
        // Aquire lock for acessing file system
        lock_acquire ( &sync_lock);
        int bytes_read = (int) file_read(pf , buffer ,size);
        // Release lock
        lock_release ( &sync_lock );
        return bytes_read;
}//end function.

static int 
write (int fd, const void *buffer, unsigned size)
{
	check( buffer );

	if( fd == 1 ){
		putbuf (buffer, size);
		return size;
	}

	if( fd == NULL || fd < 0 || fd > 129 ||thread_current()->map[fd] == NULL  ){
		kill_process();
	}
	
	if (!is_user_vaddr(fd))
		kill_process();


	struct thread *t = thread_current();
	 struct file* pf = t->map[fd]->f;

	lock_acquire( &sync_lock );
	int x = (int) file_write( pf, buffer, size );
	
	lock_release( &sync_lock );
	return  x;
	//}//end else.

}//end function.

static void
seek (int fd, unsigned position)
{
       
       if( fd == NULL || fd < 0 || fd > 129 ||thread_current()->map[fd] == NULL  ){
			kill_process();
		}
 
        // Check if position before the begining
        if(position < 0)
                kill_process();
 
        // Aquire lock for acessing file system
        lock_acquire ( &sync_lock);
 
        // Now get the requested file
        struct thread* t = thread_current();
       
        // File seek
        file_seek (t->map[fd]->f, (off_t)position);
        
        // Release lock
        lock_release ( &sync_lock );
       
}//end function.
 
static unsigned
tell (int fd)
{	   
        if( fd == NULL || fd < 0 || fd > 129 ||thread_current()->map[fd] == NULL  ){
			kill_process();
		}
 
        // Aquire lock for acessing file system
        lock_acquire ( &sync_lock);
 
        // Now get the requested file
        struct thread* t = thread_current();
       
        // Because index resembles the fd , Checks the fd place
        if (t->map[fd] != NULL){
                // File seek
                unsigned to_return = (unsigned)file_tell (t->map[fd]->f);
                // Release lock
                lock_release ( &sync_lock );
 
                return to_return;
        }
 
        // The given fd is not taken
        kill_process();
 
 
}//end function.
 
static void
close (int fd)
{
        // Check if file descriptor is in user address space
 		
 		struct thread * t = thread_current();

 		if( fd == NULL || fd < 0 || fd > 129 ||thread_current()->map[fd] == NULL  ){
			kill_process();
		}

        // Aquire lock for acessing file system
        lock_acquire ( &sync_lock);
 
        // Now get the requested file
 
        // Make the place indexed fd null
        free( t->map[fd] );
        t->map[fd] = NULL;
	 	
        // Release lock
        lock_release ( &sync_lock );
 
 
}//end function.



/*--------------------------------------ADDED METHODS----------------------------------*/


/* This method used to validate the stack ptr by :
	1 - Check if the ptr is null
	2 - Check that it's virtual address is not null and it's within user address space
*/
static void
check(void *esp)
{

	/* First check if the stack ptr is null */
	if (esp == NULL)
		kill_process();

	/* Second check that it has a valid mapping :
		1 - get current thread page directory --> call pagedir:active_pd (void)
		2 - get virtual address --> call pagedir:pagedir_get_page 
		(uint32_t *pd, const void *uaddr)
		3 - check that the returned address is not null
	*/

	// Get page directory of current thread
	uint32_t * pd = thread_current()->pagedir;

	/* Second check that user pointer points below PHYS_BASE --> user virtual address */
	if (!is_user_vaddr(esp))
		kill_process();

	// Get virtual address
	void* potential_address = pagedir_get_page (pd, esp);

	// Check if it's mapped to null
	if (potential_address == NULL)
		kill_process();
	
	/* All true */
}

/*get the needed arguments given the number of the 
arguments needed */
static void
get_Args(void* esp , int args_count , void** arg_0, void ** arg_1 , void ** arg_2 ){

	if(args_count>0)
	{
		*arg_0 =   *(void **)esp;
		esp += 4;
		check(esp);
	}

	if(args_count>1)
	{
		*arg_1 = *(void **)esp;
		esp += 4;
		check(esp);
		
	}

	if(args_count>2)
	{
		*arg_2 = *(void **)esp;
			
	}

}//end function.

static void
kill_process(){
	exit(-1);
}//end function.

void call_exit(int status){
	exit(-1);
}