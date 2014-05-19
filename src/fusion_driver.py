#!/usr/bin/python

import subprocess
import sys

example_config = {
#    'versions'             : [ "01ref_matlab" ],
    'versions'             : [ "02baseline" ],
    'do_benchmark'         : False,
    'logtofile'            : False,
    'cost_measure'         : True,
    'optimization_flags'   : "-O3 -m64 -march=native -mno-abm -fno-tree-vectorize",
    'debug'                : False,
    'gprof'                : True,
    'warmup_count'         : 1,
    'warmup_benchmark'     : 5,
    'openmode'             : 'a',
#	'driver_args'          : "--store zzz --val ../testdata/house_out/A-3-1-1-1.tif --threshold 0.1 752:1:752 500:1:500 1.0 1.0 1.0 ../testdata/srcImages/A.0.tif ../testdata/srcImages/A.1.tif ../testdata/srcImages/A.2.tif ../testdata/srcImages/A.3.tif"
    'driver_args'          : "--s zzz --v ../testdata/house_out/A-3-1-1-1.tif --w 752 --h 500 --t 0.1 "
                             "752:25:752 500:25:500 "
                             "1.0 1.0 1.0 ../testdata/srcImages/A.0.tif ../testdata/srcImages/A.1.tif ../testdata/srcImages/A.2.tif ../testdata/srcImages/A.3.tif"

}

def add_config(key, val, f):
	print >> f, "%-10s = %s" % (key, val)

def update_for_benchmark_cost(config):
	config['cost_measure'] = True
	config['debug'] = False
	config['gprof'] = False
	config['warmup_count'] = 0
	config['read_flops'] = False

def update_for_benchmark_performance(config):
	config['cost_measure'] = False
	config['debug'] = False
	config['gprof'] = False
	config['warmup_count'] = config['warmup_benchmark']
	config['read_flops'] = True

def write_config(version, config):
	with open("Make.sysconfig", "w") as f:
		add_config("VERSION", version, f)

		if config['cost_measure']:
			cost = "-DCOST_MEASURE"
		else:
			cost = ""
		add_config("CF_COST", cost, f)

		add_config("CF_WARMUP", "-DWARMUP_COUNT="+str(config['warmup_count']), f)
		add_config("CF_OPT", config['optimization_flags'], f)		

		debug = ""
		if config['debug']:
			# enable debugging output
			debug += " -DDEBUG -g"
		else:
			#disable all debugging output
			debug += " -DNDEBUG"
		if config['gprof']:
			debug += " -pg"
		if 'read_flops' in config and config['read_flops']:
			debug += " -DREADFLOPS"
		add_config("CF_DEBUG", debug, f)

		add_config("CF_CONFIG", "$(CF_OPT) $(CF_COST) $(CF_WARMUP) $(CF_DEBUG)", f)

def title(t):
	print 70*'-'
	print t
	print 70*'-'

def build_and_run(config):
	title("Doing Builds")
	for version in config['versions']:
		print "%s: writing build configuration" % version
		write_config(version, config)
		logfile = version + ".build.log"
		print "%s: cleaning " % version
		with open(logfile, config['openmode']) as log:
			if config['logtofile']:
				ret = subprocess.call(["make", "-fMake.system", "clean"], stdout=log)
			else:
				ret = subprocess.call(["make", "-fMake.system", "clean"])
			sys.stdout.flush()
			log.flush()
			if ret == 0:
				print "%s: clean successful. output in %s" % (version, logfile)
			else:
				print "%s: ERROR cleaning. see %s for details" % (version, logfile)

	for version in config['versions']:
		print "%s: writing build configuration" % version
		write_config(version, config)
		logfile = version + ".build.log"
		print "%s: building" % version
		with open(logfile, config['openmode']) as log:
			if config['logtofile']:
				ret = subprocess.call(["make", "-fMake.system"], stdout=log)
			else:
				ret = subprocess.call(["make", "-fMake.system"])
			sys.stdout.flush()
			log.flush()
			if ret == 0:
				print "%s: build successful. output in %s" % (version, logfile)
			else:
				print "%s: ERROR building. see %s for details" % (version, logfile)

	title("Running Builds")
	for version in config['versions']:
		print "%s: running" % version
		binary = "./bin/driver_" + version
		runfile = version + ".run.log"
		with open(runfile, config['openmode']) as rf:
			arg = binary + " " + config['driver_args']
			#print arg
			#arg = arg.split()
			if config['logtofile']:
				rf.write(arg+'\n')
				ret = subprocess.call(arg, stdout=rf, shell=True)
			else:
				print arg
				ret = subprocess.call(arg, shell=True)
			sys.stdout.flush()
			rf.flush()
			if ret == 0:
				print "%s: run successful. output in %s" % (version, runfile)
				if config['gprof']:
					title("Profiling Builds")
					for version in config['versions']:
						print "%s: profiling" % version
						binary = "./bin/driver_" + version
						profilefile = version + ".profile.log"
						with open(profilefile, config['openmode']) as pf:
							arg = "gprof " + binary + " gmon.out"
							if config['logtofile']:
								pf.write(arg+'\n')
								ret = subprocess.call(arg, stdout=pf, shell=True)
							else:
								print arg
								ret = subprocess.call(arg, shell=True)
							sys.stdout.flush()
							pf.flush()
							if ret == 0:
								print "%s: profiling successful. output in %s" % (version, profilefile)
							else:
								print "%s: ERROR profiling. see %s for details" % (version, profilefile)

			else:
				print "%s: ERROR running. see %s for details" % (version, runfile)

	title("Done")

if __name__ == "__main__":
	config = example_config
	title("Configuration")
	print "Building and Running: %s" % config['versions']
	print "Build Config:"
	for key in config:
		print "  %-20s : %s" % (key, config[key])

	if config['do_benchmark']:
		title("BENCHMARK: MEASURING COSTS")
		update_for_benchmark_cost(config)
		build_and_run(config)
		title("BENCHMARK: MEASURING PERFORMANCE")
		update_for_benchmark_performance(config)
		build_and_run(config);
		title("BENCHMARK: DONE")

	else:
		build_and_run(config)

