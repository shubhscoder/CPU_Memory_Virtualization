/*
In every iteration

1. Get statistics
2. Reclaim memory to host from vms which are not using it.
3. Give it to the sources which need it.


a. Make sure that the VMs and the host has sufficient memory after any release of memory.
b. Memory should be released gradually. For example, if VM has 300 MB of memory, do not release 200 MB of memory at one-go.
c. Domain should not release memory if it has less than or equal to 100MB of unused memory.
d. Host should not release memory if it has less than or equal to 200MB
*/

#include<stdio.h>
#include<stdlib.h>
#include<libvirt/libvirt.h>
#include<math.h>
#include<string.h>
#include<unistd.h>
#include<limits.h>
#include<signal.h>
#include<assert.h>

#define MIN(a,b) ((a)<(b)?a:b)
#define MAX(a,b) ((a)>(b)?a:b)

int is_exit = 0; // DO NOT MODIFY THE VARIABLE
unsigned long long host_min_unused_threshold = 300000; // All units in kbs
unsigned long long domain_min_unused_threshold = 150000;
unsigned long long domain_total_threshold = 200000;
unsigned long long give = 64000;
unsigned long long domain_max_threshold = 2000000;

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

struct Host {
	// VIR_NODE_MEMORY_STATS_FREE
	unsigned long long free_mem;

	// VIR_NODE_MEMORY_STATS_TOTAL
	unsigned long long total;	
};

struct Host hostStats;
struct VM* vms;
int numDomains;
int statsTime = 2;
int iteration = 0;
int crunch = 0;

void SetStatsCollectionTime();
void GetAllVirtualDomains(virConnectPtr conn);
void MemoryScheduler(virConnectPtr conn,int interval);

/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
void signal_callback_handler()
{
	printf("Caught Signal");
	is_exit = 1;
}


/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
int main(int argc, char *argv[])
{
	virConnectPtr conn;

	if(argc != 2)
	{
		printf("Incorrect number of arguments\n");
		return 0;
	}

	// Gets the interval passes as a command line argument and sets it as the STATS_PERIOD 
	// for collection of balloon memory statistics of the domains
	int interval = atoi(argv[1]);
	
	conn = virConnectOpen("qemu:///system");
	if(conn == NULL)
	{
		fprintf(stderr, "Failed to open connection\n");
		return 1;
	}

	signal(SIGINT, signal_callback_handler);
	GetAllVirtualDomains(conn);
	SetStatsCollectionTime();
	while(!is_exit)
	{
		// Calls the MemoryScheduler function after every 'interval' seconds
		MemoryScheduler(conn, interval);
		sleep(interval);
	}

	// Close the connection
	virConnectClose(conn);
	return 0;
}

// This function returns the list of all domains in the hypervisor
void GetAllVirtualDomains(virConnectPtr conn) {
    virDomainPtr *domains;
    int flags = VIR_CONNECT_LIST_DOMAINS_ACTIVE;
    numDomains = virConnectListAllDomains(conn, &domains, flags);
    if (numDomains < 0) {
        fprintf(stderr, "Failed to get list of domains\n");
        exit(1);
    }

	assert(domains);
	vms = calloc(numDomains, sizeof(struct VM));
	for (int i = 0; i < numDomains; i++) {
		vms[i].domain = domains[i];
		vms[i].id = i;
	}

	free(domains);    
}

// Sets the time period after which we want to collect the memory statistics.
void SetStatsCollectionTime() {
	unsigned int flags = VIR_DOMAIN_AFFECT_LIVE;
	for (int i = 0; i < numDomains; i++) {
		int ret =  virDomainSetMemoryStatsPeriod(vms[i].domain, statsTime, flags);
		if (ret != 0) {
			printf("Failed to set memory stats period for vm : %d\n", i);
			exit(1);
		}
	}
}

// This function is responsible for collected memory statistics from the hypervisor. 
void CollectMemoryUsageForDomains() {
	for (int i = 0; i < numDomains; i++) {
		virDomainMemoryStatPtr stats = calloc(VIR_DOMAIN_MEMORY_STAT_NR, sizeof(virDomainMemoryStatStruct));
		int ret = virDomainMemoryStats(vms[i].domain, stats, VIR_DOMAIN_MEMORY_STAT_NR, 0);
		if (ret < 0) {
			printf("Failed to collect memory stats for %d\n", i);
			exit(1);
		}

		for (int j = 0; j < ret; j++) {
			if (stats[j].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED) {
				vms[i].unused_memory = stats[j].val;
			}

			if (stats[j].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON) {
				vms[i].available = stats[j].val;
			}

			if (stats[j].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON) {
				vms[i].balloon = stats[j].val;
			}
		}
		free(stats);
	}

	for (int i = 0; i < numDomains; i++) {
		fprintf(stderr, "Domain %d\n", i);
		fprintf(stderr, "Unused : %llu,  Available : %llu\n\n", vms[i].unused_memory, vms[i].available);
	}
}

// Gets the memory usage statistics for the host on which the hypervisor is running.
void GetMemoryStatsForNode(virConnectPtr conn) {
	int nparams = 0;
	int cellNum = VIR_NODE_MEMORY_STATS_ALL_CELLS;
	int ret = virNodeGetMemoryStats(conn, cellNum, NULL, &nparams, 0);
	if (ret != 0) {
		printf("Failed to get memory stats\n");
		exit(1);
	}

	virNodeMemoryStatsPtr params = calloc(nparams, sizeof(virNodeMemoryStats));
	if (params == NULL) {
		printf("Failed to allocate requested memory\n");
		exit(1);
	}

	ret = virNodeGetMemoryStats(conn, cellNum, params, &nparams, 0);
	if (ret != 0) {
		printf("Failed to get memory stats\n");
		exit(1);
	}

	for (int i = 0; i < nparams; i++) {
		if (strcmp(params[i].field, VIR_NODE_MEMORY_STATS_FREE) == 0) {
			hostStats.free_mem = params[i].value;
		}

		if (strcmp(params[i].field, VIR_NODE_MEMORY_STATS_TOTAL) == 0) {
			hostStats.total = params[i].value;
		}
	}

	free(params);
}

int compare(const void* a, const void* b) {
	return (((struct VM*)a)->unused_memory < ((struct VM*)b)->unused_memory) - 
		(((struct VM*)a)->unused_memory > ((struct VM*)b)->unused_memory);
}

void newClaim() {
	for (int i = 0; i < numDomains; i++) {
		long double unused = vms[i].unused_memory;
		long double available = vms[i].available;
		long double ratio = (unused / available);

		int to_claim = 0;

		if (crunch == 1) {
			to_claim = 1;
		}

		if (ratio > 0.3) {
			to_claim = 1;
		}

		if (unused > domain_min_unused_threshold + 20000) {
			to_claim = 1;
		}

		if (available >= domain_max_threshold) {
			to_claim = 1;
		}

		if (to_claim == 1) {
			long double to_add = unused;
			to_add = MAX(to_add * 0.8, domain_min_unused_threshold + 10000);

			if (to_add > unused) {
				// if to_add is greater, then it will give memory, so do nothing
				continue;
			}
			
			long double memory = (available - unused) + to_add;
			printf("Setting memory for %d : Available : %llu, Setting : %llu\n", i, 
				(unsigned long long) available, (unsigned long long) memory);
			int ret = virDomainSetMemory(vms[i].domain, (unsigned long long) memory);
			if (ret < 0) {
				printf("Claim : Failed to set memory for domains % d\n", i);
				exit(1);
			}
		}
	}
	crunch = 0;
}

void newGive() {
	qsort(vms, numDomains, sizeof(struct VM), compare);
	unsigned long long host_free = 0;
	if (hostStats.free_mem > host_min_unused_threshold) {
		host_free = hostStats.free_mem - host_min_unused_threshold;
	}
	for (int i = 0; i < numDomains; i++) {
		if (vms[i].available >= domain_max_threshold) {
			// max threshold reached. Cannot give more memory
			continue;
		}

		if (vms[i].unused_memory < domain_min_unused_threshold) {
			unsigned long long memory = vms[i].available + give;
			if (host_free > give) {
				printf("Setting memory for %d : %llu\n", i, memory);
				host_free -= give;
				int ret = virDomainSetMemory(vms[i].domain, memory);
				if (ret < 0) {
					printf("Claim : Failed to set memory for domains % d\n", i);
					exit(1);
				}
			} else {
				printf("%llu %llu %llu\n", hostStats.free_mem, host_min_unused_threshold, 
					hostStats.free_mem - host_min_unused_threshold);
				crunch = 1;
				break;
			}
		}
	}
}

void MemoryScheduler(virConnectPtr conn, int interval)
{
	printf("\n\n Iteration %d \n\n", iteration);

	// Collect memory statistics for the host.
	GetMemoryStatsForNode(conn);

	// Collect memory statistics for the virtual machines.
	CollectMemoryUsageForDomains();

	// From the statistics collected in CollectMemoryUsageForDomains, we might get some machines,
	// that are not using the allocated memory. We can claim the memory back from these guest VMs.
	newClaim();

	// Similarly, we might get some machines that are in need of memory, we allocate some memoryb
	// which the host has to these machines.
	newGive();

	iteration++;
}