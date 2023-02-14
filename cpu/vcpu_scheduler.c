/*

## Domains is an instace of os running on a vm provided by the hypervisor. This term is used interchangeable throughout the code.

## Hypervisor needs to allocate certain amount of cpu time to each VM. Lets say t1, t2, t3, t4 ... tn.


1. The first thing you need to do is to connect to the Hypervisor, virConnect* functions in libvirt-host are what you need to check. 
   In our project, please connect to the local one which is "qemu:///system".
2. Next, you need to get all active running virtual machines within "qemu:///system", virConnectList* functions will help you.
3. You are ready to collect VCPU statistics, to do this you need virDomainGet* functions in libvirt-domain. If you also need host 
   pcpu information, there are also APIs in libvirt-host.
4. You are likely to get VCPU time in nanoseconds instead of VCPU usage in % form. Think how to transform or use them.
5. You can also determine the current map (affinity) between VCPU to PCPU through virDomainGet* functions.
6. Write your algorithm, and according to the statistics, find "the best" PCPU to pin each VCPU.
7. Use virDomainPinVcpu to dynamically change the PCPU assigned to each VCPU.
8. Now you have a "one-time scheduler", revise it to run periodically.
9. Launch several virtual machines and launch test workloads in every virtual machine to consume CPU resources, then test your VCPU
   scheduler.
*/

#include<assert.h>
#include<stdio.h>
#include<stdlib.h>
#include<libvirt/libvirt.h>
#include<math.h>
#include<string.h>
#include<unistd.h>
#include<limits.h>
#include<signal.h>
#define MIN(a,b) ((a)<(b)?a:b)
#define MAX(a,b) ((a)>(b)?a:b)

int is_exit = 0; // DO NOT MODIFY THIS VARIABLE

// CPU parameter name in CPUUsage structure returned by libvirt
const char* cpu_field_name = "cpu_time";

// Interval after which the cpu stats are collected and recalculated.
int interval;

// Conversion from secs to nano seconds. TODO: Write a utility to encapsualte all of this.
unsigned int secsToNano = 1000000000;

// An array to store all the available virtual machines.
virDomainPtr *domains;

// Number of VMs that are discovered in the virConnectListAllDomains call.
int numDomains = 0;

// Number of physical CPUs present on the host machine where the hypervisor runs.
int pcpus = 0;

// CPU bitmap to get number of pcpus.
unsigned char *cpu_map;

// Number of VMs are that actually online and need resources.
unsigned int online;

// Physical CPU utilization calculated across all the VMs.
double* pcpuFromvms;


struct PCPU {
	// ID of the CPU
	int id;

	// Percentage of utilization
	double utilization;

	// Time spent on the CPU in the current iteration.
	unsigned long long current;

	// Time spent on the CPU till the previous iteration.
	unsigned long long previous;
};

// Map between the virtual CPU to physical CPU id.
struct VCPU {
	// ID of the VCPU
	int id;

	// Physical cpu ID
	int physical_cpu_id;
};

// A structure to encapsulate information related to a single VM.
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

struct VM* vms;
struct PCPU* PCPUs;


void CPUScheduler(virConnectPtr conn,int interval);
void InitPCPUInfo(virConnectPtr conn);

/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
void signal_callback_handler()
{
    printf("Caught Signal");
    is_exit = 1;
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
		vms[i].prevAvailable = 0;
		vms[i].current = 0;
		vms[i].previous = 0;
		vms[i].id = i;
		vms[i].utilization = 0;
	}

	free(domains);
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
    interval = atoi(argv[1]);
    
    conn = virConnectOpen("qemu:///system");
    if(conn == NULL)
    {
        fprintf(stderr, "Failed to open connection\n");
        return 1;
    }

    // Get the total number of pCpus in the host
    signal(SIGINT, signal_callback_handler);

    GetAllVirtualDomains(conn);
	InitPCPUInfo(conn);
    while (!is_exit)
    // Run the CpuScheduler function that checks the CPU Usage and sets the pin at an 
	// interval of "interval" seconds
    {
        CPUScheduler(conn, interval);
        sleep(interval);
    }

    // Closing the connection
    virConnectClose(conn);
    return 0;
}

void InitPCPUInfo(virConnectPtr conn) {
	pcpus = virNodeGetCPUMap(conn, &cpu_map, &online, 0);

	PCPUs = calloc(pcpus, sizeof(struct PCPU));
	for (int i = 0; i < pcpus; i++) {
		PCPUs[i].utilization = 0;
		PCPUs[i].current = 0;
		PCPUs[i].previous = 0;
	}
}

unsigned long long GetCpuUsageFromParams(virTypedParameterPtr params, int nparams) {
	for (int i = 0; i < nparams; i++) {
		if (strcmp(params[i].field, cpu_field_name) == 0) {
			return params[i].value.ul;
		}
	}

	return 0;
}

void GetAndPrintCPUStats(virConnectPtr conn) {
    for (int i = 0; i < numDomains; i++) {
        int nparams = virDomainGetCPUStats(vms[i].domain, NULL, 0, -1, 1, 0);
        virTypedParameterPtr params = calloc(nparams, sizeof(virTypedParameter));
        virDomainGetCPUStats(vms[i].domain, params, nparams, -1, 1, 0); // total cpu stats
		vms[i].current = GetCpuUsageFromParams(params, nparams);
    }
}

void GetCpuUsagePercentage() {
	for (int i = 0; i < numDomains; i++) {
		double cpu = (100.0 * (vms[i].current - vms[i].previous)) / (1LL * interval * secsToNano); 

		vms[i].prevAvailable = 1;
		if (vms[i].previous != 0) {
			vms[i].utilization = cpu;
		}

		vms[i].previous = vms[i].current;
	}
}

void GetVcpuInfo(int hostCpus) {
	int flags = VIR_DOMAIN_VCPU_MAXIMUM;
	int cpumaplen = VIR_CPU_MAPLEN(hostCpus);

	for (int i = 0; i < numDomains; i++) {
		int vcpus = virDomainGetVcpusFlags(vms[i].domain, flags);
		vms[i].nvcpus = vcpus;
		vms[i].cur_vcpus = virDomainGetVcpusFlags(vms[i].domain, 0);

		virVcpuInfoPtr cpuInfo = calloc(vcpus, sizeof(virVcpuInfo));
		unsigned char* cpumaps = calloc(vcpus, cpumaplen);
		int infoFilled = virDomainGetVcpus(vms[i].domain, cpuInfo, vcpus, cpumaps, cpumaplen);
		for (int j = 0; j < infoFilled; j++) {
			pcpuFromvms[cpuInfo[j].cpu] += (vms[i].utilization);
		}
	}
}

int isSchedulingRequired() {
	long double mean_cpu = 0;
	for (int i = 0; i < pcpus; i++) {
		mean_cpu += (PCPUs[i].utilization);
	}

	mean_cpu /= (1.0 * pcpus);

	long double std_dev = 0;
	for (int i = 0; i < pcpus; i++) {
		std_dev += ((PCPUs[i].utilization - mean_cpu) * (PCPUs[i].utilization - mean_cpu));
	}

	std_dev /= pcpus;
	std_dev = sqrt(std_dev);

	printf("STD deviation : %Lf\n", std_dev);
	if (std_dev <= 0.05 * mean_cpu) {
		return 0;
	}

	return 1;
}

void GetPcpuUtilization(virConnectPtr conn, int pcpus) {
	for (int i = 0; i < pcpus; i++) {
		PCPUs[i].utilization = pcpuFromvms[i];
	}
}

int getMinElementIndex(double* utilization, int n) {
	double mini = 1000000000;
	int ind = -1;
	for(int i = 0; i < n; i++) {
		if (utilization[i] < mini) {
			mini = utilization[i];
			ind = i;
		}
	}

	if (ind == -1) {
		printf("Disaster\n");
	}

	return ind;
}

int compare(const void* a, const void* b) {
	return (((struct VM*)a)->utilization < ((struct VM*)b)->utilization) - 
		(((struct VM*)a)->utilization > ((struct VM*)b)->utilization);
}

void PinCPUStoVcpus() {
	int current_cpu = 0;
	double* cpu_utilization = calloc(pcpus, sizeof(double));
	// sort vms according to CPU utilization.
	qsort(vms, numDomains, sizeof(struct VM), compare);

	for (int i = 0; i < numDomains; i++) {
		for (int j = 0; j < vms[i].cur_vcpus; j++) {
			current_cpu = getMinElementIndex(cpu_utilization, pcpus);
			cpu_utilization[current_cpu] += (vms[i].utilization / (1.0 * vms[i].cur_vcpus));
			unsigned char cpumap;
			cpumap =  (0x1 << (current_cpu));

			int ret = virDomainPinVcpu(vms[i].domain, j, &cpumap, sizeof(cpumap));
			if (ret != 0) {
				printf("Failed to pin vcpu %d for VM %d to cpu %d\n", j, i, current_cpu);
				exit(1);
			}
		}
	}
}

/* COMPLETE THE IMPLEMENTATION */
void CPUScheduler(virConnectPtr conn, int interval)
{
    // Get the list of all running virtual machines
    GetAndPrintCPUStats(conn);
	GetCpuUsagePercentage();

	pcpuFromvms = calloc(pcpus, sizeof(double));
	GetVcpuInfo(pcpus);
	GetPcpuUtilization(conn, pcpus);

	int schedule = isSchedulingRequired();

	if (schedule) {
		PinCPUStoVcpus();
	}
}