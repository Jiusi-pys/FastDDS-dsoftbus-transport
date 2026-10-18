"""Autonomous public Action business across one externally managed process exit."""
import argparse
import hashlib
from pathlib import Path
import runpy
import sys
import time

BASE = Path(__file__).with_name('action_restart_probe.py')
if hashlib.sha256(BASE.read_bytes()).hexdigest() != 'f8713966b5d89473daa08e313e96f772e75726402854f7bd6ce6ff5c680e8c3f':
    raise ValueError('Qualified R30 probe changed')
R30 = runpy.run_path(str(BASE))
identity, sequence = R30['identity'], R30['sequence']


class NaturalActionRestartProbe(R30['ActionRestartProbe']):
    def control(self, phase):
        raise RuntimeError('Host Action business permissions are forbidden')

    def execute_goal(self, handle):
        from example_interfaces.action import Fibonacci
        phase = next((p for p in ('PRE', 'OLD', 'NEW') if
                      (bytes(handle.goal_id.uuid), handle.request.order) == identity(self.args.scenario, p)), None)
        allowed = ('PRE', 'OLD') if self.args.instance == 'B0' else ('NEW',)
        if phase not in allowed or phase in self.handles:
            raise RuntimeError('Foreign/duplicate goal or wrong server generation')
        self.handles[phase], self.feedback[phase] = handle, 0
        self.content('SERVER_GOAL', phase, handle.request, status=handle.status)
        values = sequence(identity(self.args.scenario, phase)[1])
        for index in range(1, len(values) - 1):
            if time.monotonic() >= self.deadline:
                raise RuntimeError('Natural goal callback deadline exceeded')
            time.sleep(0.2)
            if time.monotonic() >= self.deadline:
                raise RuntimeError('Natural goal work step crossed deadline')
            feedback = Fibonacci.Feedback(sequence=values[:index + 2])
            handle.publish_feedback(feedback)
            self.feedback[phase] = index
            self.content('SERVER_FEEDBACK', phase, feedback, index=index)
        handle.succeed()
        result = Fibonacci.Result(sequence=values)
        self.completed.add(phase)
        self.content('SERVER_RESULT', phase, result, status=handle.status)
        return result

    def on_feedback(self, phase, message):
        uuid, order = identity(self.args.scenario, phase)
        values = list(message.feedback.sequence)
        index = len(values) - 2
        if (phase not in self.goals or bytes(message.goal_id.uuid) != uuid or
                not self.feedback[phase] < index <= order - 1 or values != sequence(order)[:index + 2]):
            raise RuntimeError('Foreign/duplicate/out-of-order/incorrect feedback')
        # Retain actual indexes even if a selected-client callback was lost.
        # The independent PRE/NEW oracle still requires every fixed-fixture prefix.
        self.feedback[phase] = index
        self.content('CLIENT_FEEDBACK', phase, message.feedback, index=index)
        if index == order - 1:
            self.emit('CLIENT_FEEDBACK_COMPLETE', phase=phase, count=index)

    def responses(self):
        for phase, future in self.goals.items():
            if phase in self.results or not future.done():
                continue
            if future.cancelled() or future.exception() is not None:
                raise RuntimeError('Goal acknowledgement Future failed')
            handle = future.result()
            uuid, order = identity(self.args.scenario, phase)
            if not handle.accepted or bytes(handle.goal_id.uuid) != uuid:
                raise RuntimeError('Goal not accepted with requested UUID')
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
            expected = sequence(identity(self.args.scenario, phase)[1])
            valid = response.status == 4 and list(response.result.sequence) == expected
            if phase == 'OLD':
                valid = valid or (response.status == 0 and list(response.result.sequence) == [])
            if not valid:
                raise RuntimeError('Unexpected result status/content; OLD cannot satisfy NEW')
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
            self.emit('ACTION_READY', phase='PRE', ready=True, client_object_id=id(self.client))
            self.send('PRE')
        if 'PRE' in self.received and 'OLD' not in self.goals:
            self.send('OLD')
        if 'OLD' in self.results and not self.unavailable and not ready:
            self.unavailable = True
            self.emit('ACTION_UNAVAILABLE', ready=False, old_done=self.results['OLD'].done(),
                      client_object_id=id(self.client))
        if self.unavailable and not self.rediscovered and ready:
            self.rediscovered = True
            self.emit('ACTION_READY', phase='NEW', ready=True, client_object_id=id(self.client))
            self.send('NEW')

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
                # Public default handle_accepted_callback invokes the finite callback.
                self.server = ActionServer(self.node, Fibonacci, self.action_name, execute_callback=self.execute_goal)
            self.emit('ENTITY_READY', name=self.action_name, interface='example_interfaces/action/Fibonacci',
                      entity='client' if self.client else 'server', qos='SELECTED_PUBLIC_DEFAULTS',
                      client_object_id=id(self.client) if self.client else 0)
            completed = False
            while time.monotonic() < self.deadline:
                if not rclpy.ok():
                    raise RuntimeError('Context stopped')
                if self.finish_requested():
                    if not completed:
                        raise RuntimeError('Normal finish before finite process work completed')
                    return
                rclpy.spin_once(self.node, timeout_sec=0.01)
                if self.client:
                    self.client_step()
                    done = {'PRE', 'NEW'} <= self.received
                else:
                    done = ({'PRE', 'OLD'} <= self.completed if self.args.instance == 'B0'
                            else 'NEW' in self.completed)
                if done and not completed:
                    completed = True
                    self.emit('PHASE_COMPLETE', phase='OLD_FINITE' if self.args.instance == 'B0' else 'NEW')
            raise RuntimeError('Fixed40s natural Action process deadline exceeded')
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
                self.emit('OLD_SERVER_FINAL', status=self.handles['OLD'].status,
                          result_set='OLD' in self.completed, remote_cancel=False)
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
    probe = NaturalActionRestartProbe(parser.parse_args(argv))
    try:
        probe.communicate()
        probe.emit('FINAL', success=True, node_created=True)
        return 0
    except BaseException as error:
        probe.emit('FAIL', reason=repr(error))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
