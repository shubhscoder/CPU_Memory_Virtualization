
## Project Instruction Updates:

1. Complete the function MemoryScheduler() in memory_coordinator.c
2. If you are adding extra files, make sure to modify Makefile accordingly.
3. Compile the code using the command `make all`
4. You can run the code by `./memory_coordinator <interval>`

### Notes:

1. Make sure that the VMs and the host has sufficient memory after any release of memory.
2. Memory should be released gradually. For example, if VM has 300 MB of memory, do not release 200 MB of memory at one-go.
3. Domain should not release memory if it has less than or equal to 100MB of unused memory.
4. Host should not release memory if it has less than or equal to 200MB of unused memory.
5. While submitting, write your algorithm and logic in this Readme.

## Algorithm and logic:


### Data structures used

1. Structure to encapsulate VM information.

```c
struct VM {
	// id of the VM.
	int id;

	// Memory usage of the VM.
	unsigned long long unused_memory;

	// Available memory.
	unsigned long long available;

	// Domain pointer.
	virDomainPtr domain;

	// Requirment of memory.
	unsigned long long requirement;

	// Baloon memory.
	unsigned long long balloon;
};

```

2. Structure to encapsulate host information.

```c
struct Host {
	// VIR_NODE_MEMORY_STATS_FREE
	unsigned long long free_mem;

	// VIR_NODE_MEMORY_STATS_TOTAL
	unsigned long long total;	
};

```
### Algorithm:

1. Get the list of all running domains using ```GetAllVirtualDomains().```
2. The ```MemoryScheduler()``` function will now run periodically.
3. In every run for ```MemoryScheduler()```
   1. Get the memory stats for the VMs using ```CollectMemoryUsageForDomains().```
   2. Now proactively claim memory from the domains using the function ```ClaimMemoryFromDomains().```
   3. The amount of memory to be claimed will be returned by ```MemoryDuringClaim().``` If there is no memory crunch i.e the host has sufficient memory, the the VMs can keep upto 2x the threshold memory with them. This is done in order to make sure that performance is not comprimised by repeatedly claiming and giving memory to the VM.
   4. If the VM has more than 2x the threshold memory, then claim 10% of their unused memory. Also for VMs who already have less than threshold memory no memory is claimed.
   5. Give memory to the domains using the function ```GiveMemoryToDomains().```If the unused memory of a source is decreasing and is less than the threshold then the VM will require 2x of difference of threshold and its current unused. 
   6. If this memory is available then give it to all the VMs.
   7. If this memory is not available with the host, then claim memory from VMs who have extra unused memory. The condition above in point 3 is overrided if there is a crunch.
   8. Once memory is claimed, calculate the host memory statistics again, and give almost equal memory to the VMs who need it.
   9. The above process is repeated.