"""Finite standard parameter services/events on the admitted native runtime."""
import argparse
import base64
import hashlib
import importlib
from pathlib import Path
import re
import runpy
import sys
import time

BASE = Path(__file__).with_name('actionbag_runtime_probe.py')
if hashlib.sha256(BASE.read_bytes()).hexdigest() != '6402cbbc836ebb5f2f8fc8b7dd9f4df10da429c142c442208df410748a0d06df':
    raise ValueError('Qualified ordinary probe changed')
OrdinaryRuntimeProbe = runpy.run_path(str(BASE))['OrdinaryRuntimeProbe']
KINDS = ('bool', 'integer', 'double', 'string', 'bytes', 'bools', 'integers', 'doubles', 'strings')
SERVICES = ('ListParameters', 'GetParameterTypes', 'DescribeParameters', 'GetParameters',
            'SetParameters', 'SetParametersAtomically')


def values(version):
    return ([True, 1099511627776, 1.25, 'initial', [b'\x00', b'\xff'], [True, False],
             [-1099511627776, 7], [-0.5, 2.25], ['alpha', '参数']]
            if version == 0 else
            [False, -1099511627777, -3.5, 'updated_参数', [b'\xff', b'\x00', b'\x7f'],
             [False, True, False], [1099511627777, -8], [0.125, -4.5], ['beta', '事件', '']])


def plain(value):
    if hasattr(value, 'get_fields_and_field_types'):
        return {k: plain(getattr(value, k)) for k in value.get_fields_and_field_types()}
    if isinstance(value, bytes):
        return list(value)
    if isinstance(value, (str, int, float, bool)) or value is None:
        return value
    return [x[0] if isinstance(x, bytes) and len(x) == 1 else plain(x) for x in value]


class ParameterProbe(OrdinaryRuntimeProbe):
    def __init__(self, args):
        super().__init__(args)
        if (not re.fullmatch('[0-9a-f]{16}', args.scenario) or args.instance != args.side or
                args.role != ('requester' if args.side == 'A' else 'responder')):
            raise ValueError('Exact paired parameter identity required')
        self.node_name = 'parameter_' + args.scenario + '_' + args.side
        self.server_name = '/parameter_' + args.scenario + '_B'
        self.prefix = 'p' + args.scenario + '.'
        self.names = [self.prefix + k for k in KINDS] + [self.prefix + 'readonly', self.prefix + 'dynamic']
        self.node, self.subscription = None, None
        self.clients, self.events, self.posts, self.calls = {}, [], [], 0
        self.standard_events = set()
        self.done = False

    def emit(self, event, **fields):
        super().emit(event, scenario=self.args.scenario, instance=self.args.instance,
                     monotonic_ns=time.monotonic_ns(), **fields)

    def capture(self, event, message, **fields):
        from rclpy.serialization import serialize_message, deserialize_message
        cdr = serialize_message(message)
        if plain(deserialize_message(cdr, type(message))) != plain(message):
            raise RuntimeError('Local standard message CDR roundtrip differs')
        self.emit(event, value=plain(message), cdr_base64=base64.b64encode(cdr).decode('ascii'),
                  cdr_sha256=hashlib.sha256(cdr).hexdigest(), cdr_bytes=len(cdr),
                  native_roundtrip_equal=True, cdr_origin='LOCAL_RCLPY_SERIALIZE_MESSAGE', **fields)

    def type_support(self):
        from rclpy.type_support import check_for_type_support
        origins, capsules = {}, {}
        for name in ('rclpy', 'rclpy._rclpy_pybind11', 'rclpy.parameter_service',
                     'rcl_interfaces.msg', 'rcl_interfaces.srv'):
            module = importlib.import_module(name)
            path = Path(module.__file__).resolve()
            member = path.relative_to(self.package).as_posix()
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            if self.manifest.get(member) != digest:
                raise RuntimeError('Selected parameter module drift')
            origins[name] = {'member': member, 'sha256': digest}
        import rcl_interfaces.srv as srv
        from rcl_interfaces.msg import ParameterEvent
        for name, cls in [('ParameterEvent', ParameterEvent)] + [(n, getattr(srv, n)) for n in SERVICES]:
            check_for_type_support(cls)
            kinds = [('message', cls)] if name == 'ParameterEvent' else [('Request', cls.Request), ('Response', cls.Response)]
            for part, msg in kinds:
                check_for_type_support(msg)
                capsules[name + '.' + part] = type(msg.__class__._TYPE_SUPPORT).__name__
                if capsules[name + '.' + part] != 'PyCapsule':
                    raise RuntimeError('Real generated native type support missing')
        self.emit('TYPE_SUPPORT', origins=origins, capsules=capsules)

    def context_is_active(self):
        import rclpy
        if time.monotonic() >= self.deadline or not rclpy.ok():
            raise RuntimeError('Fixed40s parameter probe deadline/context failure')
        if self.finish_requested():
            if not self.done:
                raise RuntimeError('Finish requested before parameter work completed')
            return False
        return True

    def tick(self):
        import rclpy
        if not self.context_is_active():
            return False
        rclpy.spin_once(self.node, timeout_sec=0.01)
        return True

    def request(self, number, service, **fields):
        import rcl_interfaces.srv as srv
        request = getattr(srv, service).Request(**fields)
        self.capture('REQUEST', request, call=number, service=service)
        future = self.clients[service].call_async(request)
        while not future.done():
            if not self.tick():
                raise RuntimeError('Finish interrupted RPC')
        if future.cancelled() or future.exception() is not None:
            raise RuntimeError('Parameter Future failed')
        response = future.result()
        self.capture('RESPONSE', response, call=number, service=service)
        self.calls += 1
        return response

    def parameter(self, name, kind, value=None):
        from rclpy.parameter import Parameter
        return Parameter(self.prefix + name, Parameter.Type(kind), value).to_parameter_msg()

    def on_event(self, message):
        if message.node != self.server_name:
            # The global standard topic also carries this client's own startup events.
            if message.node != '/' + self.node_name:
                raise RuntimeError('Foreign node on parameter event topic')
            return
        parameters = list(message.new_parameters) + list(message.changed_parameters) + list(message.deleted_parameters)
        if any(not p.name.startswith(self.prefix) for p in parameters):
            from rclpy.parameter import Parameter
            defaults = {'use_sim_time': False, 'start_type_description_service': True}
            name = parameters[0].name
            if name not in defaults or name in self.standard_events:
                raise RuntimeError('Unknown/duplicate standard startup event')
            expected = plain(Parameter(name, Parameter.Type.BOOL, defaults[name]).to_parameter_msg())
            if (plain(message.new_parameters) != [expected] or message.changed_parameters or
                    message.deleted_parameters):
                raise RuntimeError('Unknown/mixed/duplicate standard startup event')
            self.capture('STANDARD_STARTUP_EVENT', message)
            self.standard_events.add(name)
            return
        self.capture('PARAMETER_EVENT', message)
        self.events.append(plain(message))

    def wait_ready(self):
        expected = {self.server_name, '/' + self.node_name}
        previous, observations = None, 0
        while True:
            if not self.context_is_active():
                raise RuntimeError('Finish interrupted parameter discovery')
            ready = [name for name in SERVICES if self.clients[name].service_is_ready()]
            matched = self.subscription.get_publisher_count()
            info = self.node.get_publishers_info_by_topic('/parameter_events')
            publishers = sorted([{'name': p.node_name, 'namespace': p.node_namespace} for p in info],
                                key=lambda p: (p['namespace'], p['name']))
            snapshot = dict(ready_services=ready, matched_publishers=matched, publishers=publishers)
            if snapshot != previous:
                if observations == 64:
                    raise RuntimeError('Parameter discovery observation limit exceeded')
                self.emit('DISCOVERY_OBSERVATION', **snapshot)
                previous, observations = snapshot, observations + 1
            names = {p['namespace'].rstrip('/') + '/' + p['name'] for p in publishers}
            if ready == list(SERVICES) and matched == 2 and len(publishers) == 2 and names == expected:
                if not self.context_is_active():
                    raise RuntimeError('Finish interrupted parameter discovery')
                return snapshot, observations
            if not self.tick():
                raise RuntimeError('Finish interrupted parameter discovery')

    def client_work(self):
        import rcl_interfaces.srv as srv
        from rclpy.qos import qos_profile_services_default, qos_profile_parameter_events
        from rcl_interfaces.msg import ParameterEvent
        self.subscription = self.node.create_subscription(ParameterEvent, '/parameter_events', self.on_event,
                                                          qos_profile_parameter_events)
        for name in SERVICES:
            suffix = re.sub(r'(?<!^)(?=[A-Z])', '_', name).lower()
            self.clients[name] = self.node.create_client(getattr(srv, name), self.server_name + '/' + suffix,
                                                          qos_profile=qos_profile_services_default)
        discovery, observations = self.wait_ready()
        self.emit('CLIENT_READY', services=discovery['ready_services'], matched_publishers=discovery['matched_publishers'],
                  publisher_fqns=sorted(p['namespace'].rstrip('/') + '/' + p['name'] for p in discovery['publishers']),
                  discovery_observations=observations,
                  event_qos={k: int(getattr(qos_profile_parameter_events, k)) for k in ('history', 'depth', 'reliability', 'durability')})
        self.request(1, 'ListParameters', prefixes=[self.prefix[:-1]], depth=0)
        self.request(2, 'GetParameterTypes', names=self.names)
        self.request(3, 'DescribeParameters', names=self.names)
        self.request(4, 'GetParameters', names=self.names)
        changed = [self.parameter(k, i, v) for i, (k, v) in enumerate(zip(KINDS, values(1)), 1)]
        changed.append(self.parameter('dynamic', 4, 'temporary'))
        self.request(5, 'SetParameters', parameters=changed)
        self.request(6, 'GetParameters', names=self.names)
        self.request(7, 'SetParametersAtomically', parameters=[self.parameter('integer', 2, 42), self.parameter('string', 4, 'atomic_参数')])
        self.request(8, 'SetParametersAtomically', parameters=[self.parameter('integer', 2, -99), self.parameter('string', 4, 'rejected')])
        self.request(9, 'SetParametersAtomically', parameters=[self.parameter('integer', 2, 123), self.parameter('readonly', 4, 'rejected')])
        self.request(10, 'SetParametersAtomically', parameters=[self.parameter('integer', 4, 'wrong_type'), self.parameter('string', 4, 'rejected')])
        self.request(11, 'GetParameters', names=self.names)
        self.request(12, 'SetParameters', parameters=[self.parameter('dynamic', 0)])
        self.request(13, 'ListParameters', prefixes=[self.prefix[:-1]], depth=0)
        self.request(14, 'GetParameters', names=self.names[:-1])
        # An observation window, not a proof that no arbitrarily late event exists.
        quiet = time.monotonic() + 0.25
        while time.monotonic() < quiet:
            self.tick()
        self.done = True
        self.emit('CLIENT_COMPLETE', calls=self.calls, observed_events=len(self.events), quiet_seconds=0.25)

    def server_work(self):
        from rclpy.parameter import Parameter
        from rcl_interfaces.msg import ParameterDescriptor, SetParametersResult
        for name, value in zip(self.names[:9], values(0)):
            self.node.declare_parameter(name, value)
        self.node.declare_parameter(self.names[9], 'fixed', ParameterDescriptor(read_only=True))
        self.node.declare_parameter(self.names[10], None, ParameterDescriptor(dynamic_typing=True))

        def validate(parameters):
            accepted = not any(p.name == self.prefix + 'integer' and p.value == -99 for p in parameters)
            self.emit('SERVER_VALIDATE', parameters=[plain(p.to_parameter_msg()) for p in parameters], accepted=accepted)
            return SetParametersResult(successful=accepted, reason='' if accepted else 'r33 callback rejects -99')

        def committed(parameters):
            self.posts.append([plain(p.to_parameter_msg()) for p in parameters])
            self.emit('SERVER_COMMIT', parameters=self.posts[-1])

        self.node.add_on_set_parameters_callback(validate)
        self.node.add_post_set_parameters_callback(committed)
        self.emit('SERVER_READY', names=self.names, server=self.server_name)
        while self.tick():
            if len(self.posts) > 12:
                raise RuntimeError('Unexpected extra parameter mutation')
            if len(self.posts) == 12 and not self.done:
                if self.node.has_parameter(self.names[-1]):
                    raise RuntimeError('Dynamic parameter not undeclared')
                self.emit('SERVER_STATE', parameters=[plain(self.node.get_parameter(n).to_parameter_msg()) for n in self.names[:-1]])
                self.done = True
                self.emit('SERVER_COMPLETE', commits=12)

    def communicate(self):
        import rclpy
        try:
            self.type_support()
            rclpy.init(args=[])
            self.node = rclpy.create_node(self.node_name, start_parameter_services=self.args.side == 'B')
            self.deadline = time.monotonic() + 40
            self.emit('NODE_READY', name='/' + self.node_name)
            if self.args.side == 'A':
                self.client_work()
                while self.tick():
                    pass
            else:
                self.server_work()
        except BaseException as error:
            self.emit('FAIL', reason=repr(error))
            raise
        finally:
            primary, errors = sys.exc_info()[1], []
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
    probe = ParameterProbe(parser.parse_args(argv))
    try:
        probe.communicate()
        probe.emit('FINAL', success=True, node_created=True)
        return 0
    except BaseException as error:
        probe.emit('FAIL', reason=repr(error))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
