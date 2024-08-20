#!/usr/bin/env python
import sys
import json

"""
Things to think about for parsing the log file
Keep track of the number of messages for info,warning,error,etc
At the device level gather all the boot information
Save data to JSON file for parsing in any other framework
"""
class ESP32Task(dict):
    def __init__(self, name, core):
        dict.__init__(self, name=name, core=int(core), data_points=[])

    def add_data_point(self, time, total_cpu, percentage, stack_hwm):
        self["data_points"].append({
                "time": int(time), 
                "total": int(total_cpu), 
                "percentage": int(percentage), 
                "stack_hwm": int(stack_hwm)
            })


class ESP32MemoryType(dict):
    def __init__(self, mem_type):
        dict.__init__(self, type=mem_type, data_points=[])

    def add_data_point(self, time, total, free, largest_free_block, lwm):
        self["data_points"].append({
            "time": int(time), 
            "total": int(total)/1000, 
            "free": int(free), 
            "lfb": int(largest_free_block), 
            "lwm": int(lwm)
        })


class ESP32LogReader:
    def __init__(self, filename):
        self.filename = filename
        self.task_info = {}
        self.memory_info = {}
        self.event_info = {}
        self.untracked_lines = []

    def read_log(self):
        with open(self.filename, "r", encoding="utf-8") as logfile:
            for line in logfile:
                stripped_line = line.strip()

                try:
                    if(stripped_line.startswith("rp-t")):
                        task_line = stripped_line.replace("rp-t,", "")
                        device_time, name, total_cpu, percentage, core, stack = task_line.split(',')
                        name_with_core = f'{name}:{core}'
                        if(not name_with_core.startswith("IDLE")):
                            if self.task_info.get(name_with_core):
                                self.task_info[name_with_core].add_data_point(device_time, total_cpu, percentage, stack)
                            else:
                                self.task_info[name_with_core] = ESP32Task(name, core)
                                self.task_info[name_with_core].add_data_point(device_time, total_cpu, percentage, stack)

                    elif(stripped_line.startswith("rp-m")):
                        memory_line = stripped_line.replace("rp-m,", "")
                        device_time,mem_type, total, free, largest_free_block, lwm = memory_line.split(',')
                        if self.memory_info.get(mem_type):
                            self.memory_info[mem_type].add_data_point(device_time, total, free, largest_free_block, lwm)
                        else:
                            self.memory_info[mem_type] = ESP32MemoryType(mem_type)
                            self.memory_info[mem_type].add_data_point(device_time, total, free, largest_free_block, lwm)

                    else:
                        self.untracked_lines.append(stripped_line)

                except ValueError as e:
                    print(e)
                    print(task_line)
                    continue

    
    def export_as_json(self):
        with open("perf_data.js", "w") as js_file:
            json_data = {'task_info':self.task_info, 'memory_info':self.memory_info, 'untracked':self.untracked_lines}
            json_str = json.dumps(json_data)
            js_file.write(f'var perf_data={json_str}')

if __name__ == "__main__":
    if(len(sys.argv) != 2):
        print("Usage: log_parser.py [path to log file]")
        sys.exit(1)

    log_reader = ESP32LogReader(sys.argv[1])
    log_reader.read_log()
    log_reader.export_as_json()