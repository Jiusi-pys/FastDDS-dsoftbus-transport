"""Three distinct public service requests across one normal server replacement."""
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


def operands(scenario, phase):
    if not re.fullmatch('[0-9a-f]{16}', scenario) or phase not in ('PRE', 'OLD', 'NEW'):
        raise ValueError('Exact scenario and request generation required')
    return int(scenario[:12], 16) * 4 + ('PRE', 'OLD', 'NEW').index(phase), 29


class ServiceRestartProbe(OrdinaryRuntimeProbe):
    def __init__(self, args):
        super().__init__(args)
        if args.instance not in ('A', 'B0', 'B1') or args.side != args.instance[0] or args.role != (
                'requester' if args.side == 'A' else 'responder'):
            raise ValueError('Service instance role mismatch')
        operands(args.scenario, 'PRE')
        self.node_name = 'service_restart_' + args.run + '_' + args.instance
        self.service_name = '/service_restart_' + args.scenario
        self.client = self.server = self.node = None
        self.futures, self.received, self.served = {}, set(), set()
        self.controls, self.responses_returned = {}, []
        self.paused = self.unavailable = self.rediscovered = False

    def emit(self, event, **fields):
        super().emit(event, scenario=self.args.scenario, instance=self.args.instance,
                     monotonic_ns=time.monotonic_ns(), **fields)

    def control(self, phase):
        if phase not in ('OLD', 'POST') or self.args.instance != 'A':
            raise ValueError('Only two finite client controls')
        path = self.root / ('control.SERVICE_' + phase)
        if not path.exists() and not path.is_symlink():
            return False
        before = path.lstat()
        if not stat.S_ISREG(before.st_mode) or stat.S_IMODE(before.st_mode) != 0o600 or before.st_size > 256:
            raise RuntimeError('Service control must be bounded regular0600')
        identity = (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns)
        text = path.read_text(encoding='ascii')
        after = path.lstat()
        if identity != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns):
            raise RuntimeError('Service control changed during read')
        if phase in self.controls and self.controls[phase] != identity:
            raise RuntimeError('Service control replaced')
        if text != ('R29_CONTROL schema=1 scenario=' + self.args.scenario + ' run=' + self.args.run +
                    ' side=A pid=' + str(os.getpid()) + ' phase=' + phase + '\n'):
            raise RuntimeError('Service control identity mismatch')
        self.controls[phase] = identity
        return True

    def content(self, event, phase, message):
        from rclpy.serialization import serialize_message
        data = serialize_message(message)
        a, b = operands(self.args.scenario, phase)
        self.emit(event, phase=phase, a=a, b=b, **({'sum': message.sum} if hasattr(message, 'sum') else {}),
                  cdr_base64=base64.b64encode(data).decode('ascii'),
                  cdr_sha256=hashlib.sha256(data).hexdigest(), cdr_bytes=len(data))

    def respond(self, request, response):
        phase = next((p for p in ('PRE', 'OLD', 'NEW') if (request.a, request.b) == operands(self.args.scenario, p)), None)
        allowed = ('PRE',) if self.args.instance == 'B0' else ('OLD', 'NEW')
        if phase not in allowed or phase in self.served:
            raise RuntimeError('Foreign/duplicate service request or wrong server generation')
        self.served.add(phase)
        self.content('SERVER_REQUEST', phase, request)
        response.sum = request.a + request.b
        self.responses_returned.append((phase, response))
        return response

    def send(self, phase):
        from example_interfaces.srv import AddTwoInts
        if phase in self.futures:
            raise RuntimeError('Application retry forbidden')
        a, b = operands(self.args.scenario, phase)
        request = AddTwoInts.Request(a=a, b=b)
        self.futures[phase] = self.client.call_async(request)
        self.content('CLIENT_REQUEST', phase, request)
        if phase == 'OLD':
            if self.futures[phase].done() or self.futures[phase].cancelled():
                raise RuntimeError('OLD request not pending at issue')
            self.emit('OLD_PENDING', checkpoint='BEFORE_B0_STOP', done=False, cancelled=False)

    def responses(self):
        for phase, future in self.futures.items():
            if phase in self.received or not future.done():
                continue
            if future.cancelled() or future.exception() is not None:
                raise RuntimeError('Unexpected service Future failure')
            response = future.result()
            a, b = operands(self.args.scenario, phase)
            if response is None or response.sum != a + b:
                raise RuntimeError('Service response generation/content mismatch')
            self.received.add(phase)
            self.content('CLIENT_RESPONSE', phase, response)

    def type_support(self):
        from example_interfaces.srv import AddTwoInts
        from rclpy.serialization import serialize_message, deserialize_message
        if sys.version_info[:3] != (3, 12, 7) or sysconfig.get_config_var('SOABI') != 'cpython-312-aarch64-linux-ohos':
            raise RuntimeError('Require selected target CPython ABI')
        capsules = {}
        for label, cls in (('Service', AddTwoInts), ('Request', AddTwoInts.Request), ('Response', AddTwoInts.Response)):
            meta = type(cls)
            meta.__import_type_support__()
            fields = ['_TYPE_SUPPORT'] if label == 'Service' else [
                '_TYPE_SUPPORT', '_CREATE_ROS_MESSAGE', '_CONVERT_FROM_PY', '_CONVERT_TO_PY', '_DESTROY_ROS_MESSAGE']
            if any(type(getattr(meta, field, None)).__name__ != 'PyCapsule' for field in fields):
                raise RuntimeError('Missing real AddTwoInts typesupport capsule: ' + label)
            capsules[label] = fields
        origins = {}
        for name in ('rclpy._rclpy_pybind11', 'rclpy.client', 'rclpy.service', 'example_interfaces.srv._add_two_ints',
                     'example_interfaces.example_interfaces_s__rosidl_typesupport_c'):
            module = importlib.import_module(name)
            path = Path(module.__file__).resolve()
            member = path.relative_to(self.package).as_posix()
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            if not member.startswith('python/') or self.manifest.get(member) != digest:
                raise RuntimeError('Unselected service module origin: ' + name)
            origins[name] = {'path': str(path), 'member': member, 'sha256': digest}
        if AddTwoInts is not importlib.import_module('example_interfaces.srv._add_two_ints').AddTwoInts:
            raise RuntimeError('Public service type does not match selected generated module')
        a, b = operands(self.args.scenario, 'PRE')
        cdr = []
        for label, cls, message, values in (
                ('Request', AddTwoInts.Request, AddTwoInts.Request(a=a, b=b), [a, b]),
                ('Response', AddTwoInts.Response, AddTwoInts.Response(sum=a+b), [a+b])):
            data = serialize_message(message)
            decoded = deserialize_message(data, cls)
            actual = [decoded.a, decoded.b] if label == 'Request' else [decoded.sum]
            if actual != values:
                raise RuntimeError('Native AddTwoInts serialize/deserialize mismatch')
            cdr.append({'type': label, 'values': actual, 'bytes': len(data),
                        'sha256': hashlib.sha256(data).hexdigest(), 'base64': base64.b64encode(data).decode('ascii')})
        self.emit('TYPE_SUPPORT_READY', interface='example_interfaces/srv/AddTwoInts', origins=origins,
                  capsules=capsules, cdr=cdr, soabi=sysconfig.get_config_var('SOABI'), node_created=False)

    def client_step(self):
        self.responses()
        ready = self.client.service_is_ready()
        if not self.futures and ready:
            self.emit('SERVICE_READY', phase='PRE', ready=True)
            self.send('PRE')
        if 'PRE' in self.received and 'OLD' not in self.futures and self.control('OLD'):
            self.emit('CONTROL_ACCEPTED', phase='OLD')
            self.send('OLD')
        if 'OLD' in self.futures and not self.unavailable and not ready:
            self.unavailable = True
            self.emit('SERVICE_UNAVAILABLE', ready=False, old_done=self.futures['OLD'].done())
        if 'OLD' in self.futures and not self.rediscovered and self.control('POST'):
            if not self.unavailable:
                raise RuntimeError('POST before observed service disappearance')
            if ready:
                self.rediscovered = True
                self.emit('CONTROL_ACCEPTED', phase='POST')
                self.emit('SERVICE_READY', phase='NEW', ready=True)
                self.send('NEW')

    def communicate(self):
        import rclpy
        from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy
        from example_interfaces.srv import AddTwoInts
        try:
            self.type_support()
            rclpy.init(args=[])
            self.node = rclpy.create_node(self.node_name)
            self.deadline = time.monotonic() + 40
            self.emit('NODE_READY', name='/' + self.node_name)
            qos = QoSProfile(depth=10, reliability=ReliabilityPolicy.RELIABLE, durability=DurabilityPolicy.VOLATILE)
            if self.args.instance == 'A':
                self.client = self.node.create_client(AddTwoInts, self.service_name, qos_profile=qos)
            else:
                self.server = self.node.create_service(AddTwoInts, self.service_name, self.respond, qos_profile=qos)
            self.emit('ENTITY_READY', name=self.service_name, interface='example_interfaces/srv/AddTwoInts',
                      entity='client' if self.client else 'service', depth=10, reliability='RELIABLE', durability='VOLATILE')
            completed = False
            while time.monotonic() < self.deadline:
                if not rclpy.ok():
                    raise RuntimeError('Context stopped')
                if self.finish_requested():
                    if not completed:
                        raise RuntimeError('Normal finish before finite service phase completed')
                    return
                if not self.paused:
                    rclpy.spin_once(self.node, timeout_sec=0.01)
                    # Executor has returned from callback AND send_response.
                    for phase, response in self.responses_returned:
                        self.content('SERVER_RESPONSE', phase, response)
                    self.responses_returned.clear()
                    if self.args.instance == 'B0' and self.served == {'PRE'}:
                        self.paused = True
                        self.emit('SERVER_PAUSED', after_callback_return=True, takes_further_requests=False)
                else:
                    time.sleep(0.01)
                if self.client:
                    self.client_step()
                done = ({'PRE', 'NEW'} <= self.received if self.client else
                        self.paused if self.args.instance == 'B0' else 'NEW' in self.served)
                if done and not completed:
                    completed = True
                    self.emit('PHASE_COMPLETE', phase='PRE' if self.args.instance == 'B0' else 'NEW')
            raise RuntimeError('Fixed40s service process deadline exceeded')
        except BaseException as error:
            self.emit('FAIL', reason=repr(error))
            raise
        finally:
            primary, errors = sys.exc_info()[1], []
            if self.client and 'OLD' in self.futures:
                try:
                    old = self.futures['OLD']
                    self.responses()
                    self.emit('OLD_FINAL_STATE', done=old.done(), cancelled=old.cancelled(),
                              response_observed='OLD' in self.received)
                    if not old.done():
                        self.client.remove_pending_request(old)
                        old.cancel()
                        self.emit('OLD_LOCAL_DISPOSE', cancelled=old.cancelled(), remote_cancel=False)
                except BaseException as error:
                    errors.append('old Future cleanup: ' + repr(error))
            for entity, method in ((self.client, 'destroy_client'), (self.server, 'destroy_service')):
                if entity is not None:
                    try:
                        if getattr(self.node, method)(entity) is not True:
                            raise RuntimeError('Public destruction returned false')
                        self.emit('ENTITY_DESTROY_RETURN', entity='client' if method == 'destroy_client' else 'service', returned=True)
                    except BaseException as error:
                        errors.append(method + ': ' + repr(error))
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
    probe = ServiceRestartProbe(parser.parse_args(argv))
    try:
        probe.communicate()
        probe.emit('FINAL', success=True, node_created=True)
        return 0
    except BaseException as error:
        probe.emit('FAIL', reason=repr(error))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
