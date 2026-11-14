"""Independent asyncua fixture, including an explicit finite Query implementation.

Query/Browse continuation extensions live only in this fixture; no gateway code
or D3 implementation is imported. Other services use asyncua's native stack.
"""
import argparse
import asyncio
from datetime import datetime, timezone, timedelta
import json
from pathlib import Path
import secrets

from asyncua import Server, ua, uamethod
from asyncua.ua.ua_binary import struct_from_binary
from asyncua.server.uaprocessor import UaProcessor
from asyncua.common.utils import ServiceError


async def serve(args):
    directory = args.directory.resolve()
    journal = []
    server = Server()
    await server.init()
    server.set_endpoint('opc.tcp://127.0.0.1:' + str(args.port))
    ns = await server.register_namespace('urn:fastdds:standards:d4:independent')
    folder = await server.nodes.objects.add_folder(ua.NodeId('Sensors', ns), ua.QualifiedName('Sensors', ns))
    nodes = []
    for suffix, value in [('A', 10), ('B', 20), ('C', 30)]:
        node = await folder.add_variable(ua.NodeId('Sensor' + suffix, ns), ua.QualifiedName('Sensor' + suffix, ns), ua.Variant(value, ua.VariantType.UInt32))
        await node.set_writable()
        await node.write_value(ua.DataValue(ua.Variant(value, ua.VariantType.UInt32), SourceTimestamp=datetime.now(timezone.utc)))
        nodes.append(node)
    await folder.add_variable(ua.NodeId('Large', ns), ua.QualifiedName('Large', ns), 'x' * 5000)

    @uamethod
    def add(parent, a, b):
        return ua.Variant(a + b, ua.VariantType.UInt32)

    await folder.add_method(ua.NodeId('Add', ns), ua.QualifiedName('Add', ns), add, [ua.VariantType.UInt32, ua.VariantType.UInt32], [ua.VariantType.UInt32])
    original = UaProcessor._process_message
    ids = {getattr(ua.ObjectIds, name + 'Request_Encoding_DefaultBinary'): name for name in
           ('Browse', 'BrowseNext', 'TranslateBrowsePathsToNodeIds', 'RegisterNodes', 'UnregisterNodes',
            'QueryFirst', 'QueryNext', 'Read', 'Write', 'HistoryRead', 'HistoryUpdate', 'Call')}

    async def process(self, typeid, header, seqhdr, body):
        name = ids.get(typeid.Identifier)
        if name:
            journal.append(dict(service=name, session=str(self.session.session_id) if self.session else None,
                                request_handle=header.RequestHandle))
            (directory / 'journal.json').write_text(json.dumps(journal, indent=2), encoding='utf-8')
        if name not in ('QueryFirst', 'QueryNext', 'Browse', 'BrowseNext', 'HistoryUpdate'):
            return await original(self, typeid, header, seqhdr, body)
        if not self.session or not self.session.is_activated():
            raise ServiceError(ua.StatusCodes.BadSessionNotActivated)
        if not hasattr(self, 'd4_cursors'):
            self.d4_cursors = {}

        def put(kind, values, page):
            if len(self.d4_cursors) >= 8:
                raise ServiceError(ua.StatusCodes.BadNoContinuationPoints)
            token = secrets.token_bytes(16)
            self.d4_cursors[token] = (kind, values, page)
            return token

        if name == 'QueryFirst':
            params = struct_from_binary(ua.QueryFirstParameters, body)
            if (len(params.NodeTypes) != 1 or params.NodeTypes[0].TypeDefinitionNode != ua.NodeId(ua.ObjectIds.BaseDataVariableType)
                    or len(params.NodeTypes[0].DataToReturn) != 1 or params.NodeTypes[0].DataToReturn[0].AttributeId != ua.AttributeIds.Value
                    or params.NodeTypes[0].DataToReturn[0].RelativePath.Elements or params.NodeTypes[0].DataToReturn[0].IndexRange):
                raise ServiceError(ua.StatusCodes.BadQueryTooComplex)
            threshold = -1
            if params.Filter.Elements:
                element = params.Filter.Elements[0]
                if (len(params.Filter.Elements) != 1 or element.FilterOperator != ua.FilterOperator.GreaterThan
                        or len(element.FilterOperands) != 2 or not isinstance(element.FilterOperands[1], ua.LiteralOperand)
                        or not isinstance(element.FilterOperands[0], ua.SimpleAttributeOperand)
                        or element.FilterOperands[0].AttributeId != ua.AttributeIds.Value
                        or element.FilterOperands[0].BrowsePath):
                    raise ServiceError(ua.StatusCodes.BadFilterOperatorUnsupported)
                threshold = element.FilterOperands[1].Value.Value
            datasets = []
            for node in nodes:
                value = await node.read_value()
                if value > threshold:
                    datasets.append(ua.QueryDataSet(NodeId=node.nodeid, TypeDefinitionNode=ua.NodeId(ua.ObjectIds.BaseDataVariableType), Values=[ua.Variant(value, ua.VariantType.UInt32)]))
            page = min(params.MaxDataSetsToReturn or 16, 16)
            response = ua.QueryFirstResponse()
            response.Parameters.QueryDataSets = datasets[:page]
            response.Parameters.ParsingResults = [ua.ParsingResult()]
            if len(datasets) > page:
                response.Parameters.ContinuationPoint = put('query', datasets[page:], page)
        elif name == 'QueryNext':
            params = struct_from_binary(ua.QueryNextParameters, body)
            cursor = self.d4_cursors.pop(params.ContinuationPoint, None)
            if cursor is None or cursor[0] != 'query':
                raise ServiceError(ua.StatusCodes.BadContinuationPointInvalid)
            response = ua.QueryNextResponse()
            if not params.ReleaseContinuationPoint:
                _, values, page = cursor
                response.Parameters.QueryDataSets = values[:page]
                if len(values) > page:
                    response.Parameters.RevisedContinuationPoint = put('query', values[page:], page)
        elif name == 'Browse':
            params = struct_from_binary(ua.BrowseParameters, body)
            response = ua.BrowseResponse()
            response.Results = await self.session.browse(params)
            if params.RequestedMaxReferencesPerNode:
                page = min(params.RequestedMaxReferencesPerNode, 16)
                for result in response.Results:
                    if len(result.References) > page:
                        result.ContinuationPoint = put('browse', result.References[page:], page)
                        result.References = result.References[:page]
        elif name == 'BrowseNext':
            params = struct_from_binary(ua.BrowseNextParameters, body)
            response = ua.BrowseNextResponse()
            for token in params.ContinuationPoints:
                result = ua.BrowseResult()
                cursor = self.d4_cursors.pop(token, None)
                if cursor is None or cursor[0] != 'browse':
                    result.StatusCode = ua.StatusCode(ua.StatusCodes.BadContinuationPointInvalid)
                elif not params.ReleaseContinuationPoints:
                    _, values, page = cursor
                    result.References = values[:page]
                    if len(values) > page:
                        result.ContinuationPoint = put('browse', values[page:], page)
                response.Parameters.Results.append(result)
        else:
            params = struct_from_binary(ua.HistoryUpdateParameters, body)
            response = ua.HistoryUpdateResponse()
            for detail in params.HistoryUpdateDetails:
                result = ua.HistoryUpdateResult()
                if not isinstance(detail, ua.UpdateDataDetails) or detail.NodeId != nodes[0].nodeid or detail.PerformInsertReplace != ua.PerformUpdateType.Insert:
                    result.StatusCode = ua.StatusCode(ua.StatusCodes.BadHistoryOperationUnsupported)
                else:
                    for value in detail.UpdateValues:
                        await self.iserver.history_manager.storage.save_node_value(detail.NodeId, value)
                        result.OperationResults.append(ua.StatusCode())
                response.Results.append(result)
        self.send_response(header.RequestHandle, seqhdr, response)
        return True

    UaProcessor._process_message = process
    try:
        async with server:
            await server.historize_node_data_change(nodes[0], period=timedelta(seconds=60), count=16)
            (directory / 'ready.json').write_text(json.dumps(dict(namespace=ns)), encoding='utf-8')
            print('D4_UA_FIXTURE_READY', flush=True)
            async with asyncio.timeout(90):
                while not (directory / 'stop').exists():
                    await asyncio.sleep(.01)
        print('D4_UA_FIXTURE_CLOSED exit=0 owners=0', flush=True)
    finally:
        UaProcessor._process_message = original


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--directory', type=Path, required=True)
    asyncio.run(serve(parser.parse_args()))
