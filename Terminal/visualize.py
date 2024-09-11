import graphviz

def parse_process_data(filepath):
    with open(filepath, 'r') as file:
        lines = file.readlines()
    print(len(lines))
    processes = {}
    prev_depth = 0
    prev_pid = -1
    smallest_time = 1000
    smallest_children_pid = -1
    depth_map = {}
    for line in lines:
        if line.lstrip(' ').startswith("Children"):
            parts = line.split(',')
            depth = (len(line) - len(line.lstrip(' '))) // 4
            pid_info = parts[0].strip().split()
            pid = pid_info[2]
            start_time = parts[2].split(':')[1].strip()
            command = parts[1].split(':')[1].strip()


            processes[pid] = {'command': command,'start_time': start_time,'is_heir': False, 'depth': depth, 'children': []}
            #Initialize the root prev_pid.
            if depth == 0:
                depth_map = {0: pid} #initialize depth map.
                processes[pid]['heir'] = True
                prev_pid = pid
                smallest_children_pid = pid
                smallest_time = start_time
                continue

            processes[depth_map[depth-1]]['children'].append(pid)

            depth_map[depth] = pid
    find_heirs(depth_map[0],processes)
    return processes



def find_heirs(pid,processes):
    smallest_time = 10**10
    heir = -1
    for pid in processes[pid]['children']:

        if int(processes[pid]['start_time'])< int(smallest_time):
            heir = pid
            smallest_time = processes[pid]['start_time']

        processes[heir]['is_heir'] = True
        find_heirs(pid,processes)




def build_graph(processes):
    dot = graphviz.Digraph(comment='Process Tree', format='png')
    for pid, info in processes.items():

        label = f"PID: {pid}\nCmd: {info['command']}\n{info['start_time']}"
        color = 'black'
        if info['is_heir']:
            color = 'red'
        dot.node(pid, label, color=color)
        #dot.node(pid, label, color='red' if pid == 453 else 'black')
        for child in info['children']:
            dot.edge(pid, child)
    dot.render('process_tree')

process_data = 'process_data.txt'
processes = parse_process_data(process_data)
build_graph(processes)
print("Graph has been generated as 'process_tree.png'")
