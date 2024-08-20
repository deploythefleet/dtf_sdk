async function hello() {
    console.log(data);
}

function mapRawDataToGraphData(data){
    var cpu_values = {core0: {total_for_core:{x_values: [], y_cpu_values: [], y_stack_values: []}}, core1: {total_for_core:{x_values: [], y_cpu_values: [], y_stack_values: []}}}
    var mem_values = {}
    for (const task in data.task_info){
        for(var i = 0; i < data["task_info"][task].data_points.length; i++){
            if(data["task_info"][task].core == 0){
                // Core 0
                if(!("core0" in cpu_values)){ cpu_values.core0 = {}}
                if(!(task in cpu_values.core0)){
                    cpu_values.core0[task] = {}
                    cpu_values.core0[task].x_values = []
                    cpu_values.core0[task].y_cpu_values = []
                    cpu_values.core0[task].y_stack_values = []
                }
                cpu_values["core0"][task].x_values.push(data["task_info"][task].data_points[i].time);
                cpu_values["core0"][task].y_cpu_values.push(data["task_info"][task].data_points[i].percentage);
                cpu_values["core0"][task].y_stack_values.push(data["task_info"][task].data_points[i].stack_hwm);

                // Add value to the CPU total
                index_of_time = cpu_values.core0.total_for_core.x_values.indexOf(data["task_info"][task].data_points[i].time);
                if(index_of_time == -1){
                    cpu_values.core0.total_for_core.x_values.push(data["task_info"][task].data_points[i].time);
                    cpu_values.core0.total_for_core.y_cpu_values.push(data["task_info"][task].data_points[i].percentage);
                }
                else{
                    cpu_values.core0.total_for_core.y_cpu_values[index_of_time] += data["task_info"][task].data_points[i].percentage;
                }
            }
            else{
                // Core 1
                if(!("core1" in cpu_values)){ cpu_values.core1 = {}}
                if(!(task in cpu_values.core1)){
                    cpu_values.core1[task] = {}
                    cpu_values.core1[task].x_values = []
                    cpu_values.core1[task].y_cpu_values = []
                    cpu_values.core1[task].y_stack_values = []
                }
                cpu_values["core1"][task].x_values.push(data["task_info"][task].data_points[i].time);
                cpu_values["core1"][task].y_cpu_values.push(data["task_info"][task].data_points[i].percentage);
                cpu_values["core1"][task].y_stack_values.push(data["task_info"][task].data_points[i].stack_hwm);

                // Add value to the CPU total
                index_of_time = cpu_values.core1.total_for_core.x_values.indexOf(data["task_info"][task].data_points[i].time);
                if(index_of_time == -1){
                    cpu_values.core1.total_for_core.x_values.push(data["task_info"][task].data_points[i].time);
                    cpu_values.core1.total_for_core.y_cpu_values.push(data["task_info"][task].data_points[i].percentage);
                }
                else{
                    cpu_values.core1.total_for_core.y_cpu_values[index_of_time] += data["task_info"][task].data_points[i].percentage;
                }
            }
        }
    }

    for (const mem_type in data.memory_info){
        for(var i = 0; i < data["memory_info"][mem_type].data_points.length; i++){
            if(!(mem_type in mem_values)){ 
                mem_values[mem_type] = {}
                mem_values[mem_type].x_values = []
                mem_values[mem_type].y_free_values = []
                mem_values[mem_type].y_lfb_values = []
                mem_values[mem_type].y_lwm_values = []
            }
            mem_values[mem_type].x_values.push(data["memory_info"][mem_type].data_points[i].time);
            mem_values[mem_type].y_free_values.push(data["memory_info"][mem_type].data_points[i].free);
            mem_values[mem_type].y_lfb_values.push(data["memory_info"][mem_type].data_points[i].lfb);
            mem_values[mem_type].y_lwm_values.push(data["memory_info"][mem_type].data_points[i].lwm);
        }
    }

    return {
        cpu_info: cpu_values,
        mem_info: mem_values
    };
}