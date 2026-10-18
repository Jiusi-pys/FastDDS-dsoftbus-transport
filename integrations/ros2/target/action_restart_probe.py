"""Three public Fibonacci goals across one normal Action server replacement."""
import argparse
import base64
import hashlib
import importlib
import os
from pathlib import Path
import re
import runpy
import stat
import sys
import sysconfig
import time

BASE = Path(__file__).with_name('actionbag_runtime_probe.py')
if hashlib.sha256(BASE.read_bytes()).hexdigest() != '6402cbbc836ebb5f2f8fc8b7dd9f4df10da429c142c442208df410748a0d06df':
    raise ValueError('Frozen ordinary probe changed')
OrdinaryRuntimeProbe = runpy.run_path(str(BASE))['OrdinaryRuntimeProbe']


def identity(scenario, phase):
    if not re.fullmatch('[0-9a-f]{16}', scenario) or phase not in ('PRE', 'OLD', 'NEW'):
        raise ValueError('Exact scenario and goal generation required')
    return hashlib.sha256(('R30/' + scenario + '/' + phase).encode('ascii')).digest()[:16], 6 + ('PRE', 'OLD', 'NEW').index(phase)


def sequence(order):
    values = [0, 1]
    for _ in range(1, order):
        values.append(values[-1] + values[-2])
    return values


class ActionRestartProbe(OrdinaryRuntimeProbe):
    def __init__(self, args):
        super().__init__(args)
        if args.instance not in ('A', 'B0', 'B1') or args.side != args.instance[0] or args.role != (
                'requester' if args.side == 'A' else 'responder'):
            raise ValueError('Action instance role mismatch')
        identity(args.scenario, 'PRE')
        self.node_name = 'action_restart_' + args.run + '_' + args.instance
        self.action_name = '/action_restart_' + args.scenario
        self.client = self.server = self.node = None
        self.goals, self.results, self.handles, self.feedback = {}, {}, {}, {}
        self.received, self.completed, self.controls = set(), set(), {}
        self.statuses = {}
        self.unavailable = self.rediscovered = self.old_pending = False
        self.old_started = False

    def emit(self, event, **fields):
        super().emit(event, scenario=self.args.scenario, instance=self.args.instance,
                     monotonic_ns=time.monotonic_ns(), **fields)

    def control(self, phase):
        allowed = ('OLD', 'POST') if self.args.instance == 'A' else (
            ('COMPLETE_PRE', 'START_OLD') if self.args.instance == 'B0' else ('COMPLETE_NEW',))
        if phase not in allowed:
            raise ValueError('Control belongs to another Action generation')
        path = self.root / ('control.ACTION_' + phase)
        if not path.exists() and not path.is_symlink():
            return False
        before = path.lstat()
        if not stat.S_ISREG(before.st_mode) or stat.S_IMODE(before.st_mode) != 0o600 or before.st_size > 256:
            raise RuntimeError('Action control must be bounded regular0600')
        inode = (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns)
        text = path.read_text(encoding='ascii')
        after = path.lstat()
        if inode != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns):
            raise RuntimeError('Action control changed during read')
        if phase in self.controls and self.controls[phase] != inode:
            raise RuntimeError('Action control replaced')
        if text != ('R30_CONTROL schema=1 scenario=' + self.args.scenario + ' run=' + self.args.run +
                    ' side=' + self.args.side + ' pid=' + str(os.getpid()) + ' phase=' + phase + '\n'):
            raise RuntimeError('Action control identity mismatch')
        self.controls[phase] = inode
        return True

    def content(self, event, phase, message, **fields):
        from rclpy.serialization import serialize_message
        data = serialize_message(message)
        uuid, order = identity(self.args.scenario, phase)
        self.emit(event, phase=phase, uuid=uuid.hex(), order=order,
                  values=[message.order] if hasattr(message, 'order') else list(message.sequence),
                  cdr_base64=base64.b64encode(data).decode('ascii'),
                  cdr_sha256=hashlib.sha256(data).hexdigest(), cdr_bytes=len(data), **fields)

    def accepted(self, handle):
        phase = next((p for p in ('PRE', 'OLD', 'NEW') if
                      (bytes(handle.goal_id.uuid), handle.request.order) == identity(self.args.scenario, p)), None)
        allowed = ('PRE', 'OLD') if self.args.instance == 'B0' else ('NEW',)
        if phase not in allowed or phase in self.handles:
            raise RuntimeError('Foreign/duplicate goal or wrong server generation')
        if phase != 'OLD':
            handle.executing()
        self.handles[phase], self.feedback[phase] = handle, 0
        self.content('SERVER_GOAL', phase, handle.request, status=handle.status)

    def server_step(self):
        from example_interfaces.action import Fibonacci
        for phase, handle in self.handles.items():
            if phase == 'OLD' and not self.old_started:
                # Wait until A has created its public goal handle. Otherwise an
                # earlier status-topic update can be discarded by rclpy.
                if not self.control('START_OLD'):
                    continue
                self.emit('CONTROL_ACCEPTED', phase='START_OLD')
                handle.executing()
                self.old_started = True
                self.emit('OLD_EXECUTING', phase='OLD', uuid=bytes(handle.goal_id.uuid).hex(), status=handle.status)
            values = sequence(identity(self.args.scenario, phase)[1])
            count = 1 if phase == 'OLD' else len(values) - 2
            if self.feedback[phase] < count:
                index = self.feedback[phase] + 1
                feedback = Fibonacci.Feedback(sequence=values[:index + 2])
                handle.publish_feedback(feedback)
                self.feedback[phase] = index
                self.content('SERVER_FEEDBACK', phase, feedback, index=index)
                if phase == 'OLD':
                    self.emit('OLD_HELD', status=handle.status, result_set=False, callback_active=False)
            elif phase != 'OLD' and phase not in self.completed and self.control('COMPLETE_' + phase):
                self.emit('CONTROL_ACCEPTED', phase='COMPLETE_' + phase)
                result = Fibonacci.Result(sequence=values)
                handle.succeed(result)
                self.completed.add(phase)
                self.content('SERVER_RESULT', phase, result, status=handle.status)

    def on_feedback(self, phase, message):
        uuid, order = identity(self.args.scenario, phase)
        if phase not in self.goals or bytes(message.goal_id.uuid) != uuid:
            raise RuntimeError('Feedback UUID does not match issued goal')
        index = self.feedback[phase] + 1
        count = 1 if phase == 'OLD' else order - 1
        if index > count or list(message.feedback.sequence) != sequence(order)[:index + 2]:
            raise RuntimeError('Duplicate/out-of-order/incorrect feedback')
        self.feedback[phase] = index
        self.content('CLIENT_FEEDBACK', phase, message.feedback, index=index)
        if index == count:
            self.emit('CLIENT_FEEDBACK_COMPLETE', phase=phase, count=count)

    def send(self, phase):
        from example_interfaces.action import Fibonacci
        from unique_identifier_msgs.msg import UUID
        if phase in self.goals:
            raise RuntimeError('Application retry forbidden')
        uuid, order = identity(self.args.scenario, phase)
        goal = Fibonacci.Goal(order=order)
        self.feedback[phase] = 0
        self.goals[phase] = self.client.send_goal_async(goal, goal_uuid=UUID(uuid=list(uuid)),
                                                      feedback_callback=lambda msg: self.on_feedback(phase, msg))
        self.content('CLIENT_GOAL', phase, goal)

    def responses(self):
        for phase, future in self.goals.items():
            if phase in self.results or not future.done():
                continue
            if future.cancelled() or future.exception() is not None:
                raise RuntimeError('Goal acknowledgement Future failed')
            handle = future.result()
            uuid, order = identity(self.args.scenario, phase)
            if not handle.accepted or bytes(handle.goal_id.uuid) != uuid:
                raise RuntimeError('Goal not accepted with the requested UUID')
            self.emit('CLIENT_ACCEPTED', phase=phase, uuid=uuid.hex(), order=order, accepted=True)
            self.handles[phase] = handle
            self.results[phase] = handle.get_result_async()
            self.emit('CLIENT_RESULT_REQUEST', phase=phase, uuid=uuid.hex())
        for phase, future in self.results.items():
            if phase in self.received or not future.done():
                continue
            if future.cancelled() or future.exception() is not None:
                raise RuntimeError('GetResult Future failed')
            response = future.result()
            expected = [] if phase == 'OLD' else sequence(identity(self.args.scenario, phase)[1])
            if response.status != (0 if phase == 'OLD' else 4) or list(response.result.sequence) != expected:
                raise RuntimeError('Unexpected result status/content; OLD cannot satisfy NEW')
            if phase == 'OLD' and not self.unavailable:
                raise RuntimeError('OLD result before old server disappearance')
            self.received.add(phase)
            self.content('CLIENT_RESULT', phase, response.result, status=response.status)

    def client_step(self):
        self.responses()
        for phase, handle in self.handles.items():
            if handle.status != self.statuses.get(phase):
                self.statuses[phase] = handle.status
                self.emit('CLIENT_STATUS', phase=phase, uuid=bytes(handle.goal_id.uuid).hex(), status=handle.status)
        ready = self.client.server_is_ready()
        if not self.goals and ready:
            self.emit('ACTION_READY', phase='PRE', ready=True)
            self.send('PRE')
        if 'PRE' in self.received and 'OLD' not in self.goals and self.control('OLD'):
            self.emit('CONTROL_ACCEPTED', phase='OLD')
            self.send('OLD')
        if ('OLD' in self.results and self.feedback['OLD'] == 1 and not self.old_pending and
                self.statuses.get('OLD') == 2):
            old = self.results['OLD']
            if old.done() or old.cancelled():
                raise RuntimeError('OLD result not pending before server close')
            self.old_pending = True
            self.emit('OLD_PENDING', checkpoint='BEFORE_B0_STOP', accepted=True, feedback_count=1, done=False, cancelled=False)
        if self.old_pending and not self.unavailable and not ready:
            self.unavailable = True
            self.emit('ACTION_UNAVAILABLE', ready=False, old_done=self.results['OLD'].done())
        if self.old_pending and not self.rediscovered and self.control('POST'):
            if not self.unavailable:
                raise RuntimeError('POST before observed Action disappearance')
            if ready:
                self.rediscovered = True
                self.emit('CONTROL_ACCEPTED', phase='POST')
                self.emit('ACTION_READY', phase='NEW', ready=True)
                self.send('NEW')

    def type_support(self):
        from example_interfaces.action import Fibonacci
        from action_msgs.srv import CancelGoal
        from action_msgs.msg import GoalStatusArray
        from unique_identifier_msgs.msg import UUID
        from rclpy.serialization import serialize_message, deserialize_message
        if sys.version_info[:3] != (3, 12, 7) or sysconfig.get_config_var('SOABI') != 'cpython-312-aarch64-linux-ohos':
            raise RuntimeError('Require selected target CPython ABI')
        classes = {'Action': Fibonacci, 'Goal': Fibonacci.Goal, 'Feedback': Fibonacci.Feedback, 'Result': Fibonacci.Result,
                   'SendGoal': Fibonacci.Impl.SendGoalService, 'GetResult': Fibonacci.Impl.GetResultService,
                   'FeedbackMessage': Fibonacci.Impl.FeedbackMessage, 'CancelGoal': CancelGoal,
                   'GoalStatusArray': GoalStatusArray, 'UUID': UUID}
        capsules = {}
        for label, cls in classes.items():
            meta = type(cls)
            meta.__import_type_support__()
            fields = ['_TYPE_SUPPORT'] if label in ('Action', 'SendGoal', 'GetResult', 'CancelGoal') else [
                '_TYPE_SUPPORT', '_CREATE_ROS_MESSAGE', '_CONVERT_FROM_PY', '_CONVERT_TO_PY', '_DESTROY_ROS_MESSAGE']
            if any(type(getattr(meta, field, None)).__name__ != 'PyCapsule' for field in fields):
                raise RuntimeError('Missing real Action typesupport capsule: ' + label)
            capsules[label] = fields
        origins = {}
        for name in ('rclpy._rclpy_pybind11', 'rclpy.action.client', 'rclpy.action.server',
                     'example_interfaces.action._fibonacci', 'example_interfaces.example_interfaces_s__rosidl_typesupport_c',
                     'action_msgs.srv._cancel_goal', 'action_msgs.msg._goal_status_array',
                     'action_msgs.action_msgs_s__rosidl_typesupport_c', 'unique_identifier_msgs.msg._uuid',
                     'unique_identifier_msgs.unique_identifier_msgs_s__rosidl_typesupport_c'):
            path = Path(importlib.import_module(name).__file__).resolve()
            member = path.relative_to(self.package).as_posix()
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            if not member.startswith('python/') or self.manifest.get(member) != digest:
                raise RuntimeError('Unselected Action module origin: ' + name)
            origins[name] = {'path': str(path), 'member': member, 'sha256': digest}
        if Fibonacci is not importlib.import_module('example_interfaces.action._fibonacci').Fibonacci:
            raise RuntimeError('Public Action differs from selected generated module')
        order = identity(self.args.scenario, 'PRE')[1]
        cdr = []
        for label, cls, message, values in (
                ('Goal', Fibonacci.Goal, Fibonacci.Goal(order=order), [order]),
                ('Feedback', Fibonacci.Feedback, Fibonacci.Feedback(sequence=sequence(order)[:3]), sequence(order)[:3]),
                ('Result', Fibonacci.Result, Fibonacci.Result(sequence=sequence(order)), sequence(order))):
            data = serialize_message(message)
            decoded = deserialize_message(data, cls)
            actual = [decoded.order] if label == 'Goal' else list(decoded.sequence)
            if actual != values:
                raise RuntimeError('Native Action serialize/deserialize mismatch')
            cdr.append({'type': label, 'values': actual, 'bytes': len(data),
                        'sha256': hashlib.sha256(data).hexdigest(), 'base64': base64.b64encode(data).decode('ascii')})
        self.emit('TYPE_SUPPORT_READY', interface='example_interfaces/action/Fibonacci', origins=origins,
                  capsules=capsules, cdr=cdr, soabi=sysconfig.get_config_var('SOABI'), node_created=False)

    def communicate(self):
        import rclpy
        from rclpy.action import ActionClient, ActionServer
        from example_interfaces.action import Fibonacci
        try:
            self.type_support()
            rclpy.init(args=[])
            self.node = rclpy.create_node(self.node_name)
            self.deadline = time.monotonic() + 40
            self.emit('NODE_READY', name='/' + self.node_name)
            if self.args.instance == 'A':
                self.client = ActionClient(self.node, Fibonacci, self.action_name)
            else:
                self.server = ActionServer(self.node, Fibonacci, self.action_name, execute_callback=None,
                                           handle_accepted_callback=self.accepted)
            self.emit('ENTITY_READY', name=self.action_name, interface='example_interfaces/action/Fibonacci',
                      entity='client' if self.client else 'server', qos='SELECTED_PUBLIC_DEFAULTS')
            completed = False
            while time.monotonic() < self.deadline:
                if not rclpy.ok():
                    raise RuntimeError('Context stopped')
                if self.finish_requested():
                    if not completed:
                        raise RuntimeError('Normal finish before finite Action phase completed')
                    return
                rclpy.spin_once(self.node, timeout_sec=0.01)
                if self.client:
                    self.client_step()
                    done = {'PRE', 'NEW'} <= self.received and all(self.statuses.get(p) == 4 for p in ('PRE', 'NEW'))
                else:
                    self.server_step()
                    done = ('PRE' in self.completed and self.feedback.get('OLD') == 1 if self.args.instance == 'B0'
                            else 'NEW' in self.completed)
                if done and not completed:
                    completed = True
                    self.emit('PHASE_COMPLETE', phase='OLD_HELD' if self.args.instance == 'B0' else 'NEW')
            raise RuntimeError('Fixed40s Action process deadline exceeded')
        except BaseException as error:
            self.emit('FAIL', reason=repr(error))
            raise
        finally:
            primary, errors = sys.exc_info()[1], []
            if self.client and 'OLD' in self.results:
                try:
                    self.responses()
                    old = self.results['OLD']
                    self.emit('OLD_FINAL_STATE', done=old.done(), cancelled=old.cancelled(),
                              response_observed='OLD' in self.received)
                    if not old.done():
                        old.cancel()
                        self.emit('OLD_LOCAL_DISPOSE', cancelled=old.cancelled(), remote_cancel=False)
                except BaseException as error:
                    errors.append('old Future cleanup: ' + repr(error))
            if self.server and 'OLD' in self.handles:
                self.emit('OLD_SERVER_ABANDON', status=self.handles['OLD'].status, result_set=False, remote_cancel=False)
            for entity, kind in ((self.client, 'client'), (self.server, 'server')):
                if entity is not None:
                    try:
                        entity.destroy()
                        self.emit('ENTITY_DESTROY_RETURN', entity=kind, returned=True)
                    except BaseException as error:
                        errors.append(kind + '.destroy: ' + repr(error))
            if self.node is not None:
                try:
                    self.node.destroy_node()
                    self.emit('NODE_DESTROY_RETURN', returned=True)
                except BaseException as error:
                    errors.append('destroy_node: ' + repr(error))
            try:
                rclpy.try_shutdown()
                self.emit('SHUTDOWN_RETURN', returned=True)
            except BaseException as error:
                errors.append('try_shutdown: ' + repr(error))
            if errors:
                self.emit('FAIL', reason='Cleanup failed', cleanup_errors=errors, primary_failure=repr(primary))
                if primary is None:
                    raise RuntimeError('; '.join(errors))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('run', 'scenario', 'instance', 'side', 'role', 'finish-control'):
        parser.add_argument('--' + name, required=True)
    probe = ActionRestartProbe(parser.parse_args(argv))
    try:
        probe.communicate()
        probe.emit('FINAL', success=True, node_created=True)
        return 0
    except BaseException as error:
        probe.emit('FAIL', reason=repr(error))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
