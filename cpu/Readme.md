
## Project Instruction Updates:

1. Complete the function CPUScheduler() in vcpu_scheduler.c
2. If you are adding extra files, make sure to modify Makefile accordingly.
3. Compile the code using the command `make all`
4. You can run the code by `./vcpu_scheduler <interval>`
5. While submitting, write your algorithm and logic in this Readme.

## Algorithm and logic:


### Data structures used

1. Structure to encapsulate PCPU information.

```c
struct PCPU {
	// ID of the CPU.
	int id;

	// Percentage of utilization of the PCPU.
	double utilization;

	// PCPU cycles spent in the current iteration.
	unsigned long long current;

	// Previous time spend.
	unsigned long long previous;
};

```

2. Structure to encapsulate VM information.

```c
struct VM {
	// ID of the VM
	int id;

	// Percentage of utilization CPU
	double utilization;

	// Current cpu utilization
	unsigned long long current;

	// Previous cpu utilization
	unsigned long long previous;

	// Domain pointer
	virDomainPtr domain;

	// Max num of VCPUs
	int nvcpus;

	// Current number of vcpus
	int cur_vcpus;

	// VCPUS that this VM is using
	struct VCPU* vcpu_arr;

	// Is previous set
	int prevAvailable;
};

```
### Algorithm:

1. Get the list of all running domains using ```GetAllVirtualDomains().```
2. Get the number of PCPUs and initialize the data structure ```InitPCPUInfo().```
3. The ```CPUScheduler()``` function will now run periodically.
4. In every run for ```CPUScheduler()```
   1. Get the VCPU stats for every virtual machine or domain using ```GetAndPrintCPUStats().```
   2. Calculate the percentage VCPU % usage using the stats collected in the current and the previous iteration using ```GetCpuUsagePercentage().```
   3. Get the current mapping of VCPUs to PCPUs using ```GetVcpuInfo().```
   4. Get the PCPU utilization as a sum of utilization of mapped VCPUs using ```GetPcpuUtilization().``` This method might not be the most accurate one as their is already an API available which gives the exact PCPU utilization. However, the python monitor script uses this method for PCPU utilization calculation.
   5. Depending on the current PCPU utilization, check if scheduling is required using the function ```isSchedulingRequired().```
   6. ```isSchedulingRequired()``` decides if scheduling is required or not using the standard deviation of the PCPU utilization numbers. If the standard deviation is greater than 5% of the mean, then the function will return ```true``` else it will return ```false```

5. Algorithm for mapping VCPU to CPU is as follows:
   1. Sort the vms according to the VCPU utilization in descending order.
   2. Now for assigning one VCPU find the PCPU with the least utilization right now.
   3. Add the utilization of the current VCPU to the found PCPU.
   4. Repeat the process for all the VCPUS.
   5. This is a greedy algorithm and it is not guranteed to always find the most optimal assignment.
   6. However, the problem mapping VCPU to CPU so as to minimize standard deviation is NP complete and there is only a non-polynomial time solution for this.  
   7. Considering this, the greedy algorithm deviates from the optimal assignment in a very small bound, and should be good enough to meet the 5% std deviation criteria.