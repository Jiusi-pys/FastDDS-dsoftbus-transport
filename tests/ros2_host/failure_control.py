"""Read the application's primary failure record from either collected stream."""
import re
from control_files import publish_control


def primary_failure_visible(stdout, stderr):
    return any(re.search(r'^ROS_FAILURE schema=1 ', text, re.M)
               or 'NATIVE_REACCEPT_REFUSAL ' in text for text in (stdout, stderr))


def release_failed_peers(output, run, processes):
    released=[]
    for side, process in processes.items():
        path=output/(side+'-control.CLEANUP')
        if process.poll() is None and not path.exists():
            publish_control(path, f'ROS_CLEANUP schema=1 run={run} side={side} pid={process.pid} stage=CLEANUP reason=WINDOW_FAILED\n')
            released.append(side)
    return released
