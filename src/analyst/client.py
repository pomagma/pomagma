import sys
import zmq
from pomagma.analyst import messages_pb2 as messages


CONTEXT = zmq.Context()

Request = messages.AnalystRequest
Response = messages.AnalystResponse

TROOL = {
    Response.MAYBE: None,
    Response.TRUE: True,
    Response.FALSE: False,
}


def WARN(message):
    sys.stdout.write('WARNING {}\n'.format(message))
    sys.stdout.flush()


class ServerError(Exception):

    def __init__(self, messages):
        self.messages = list(messages)

    def __str__(self):
        return '\n'.join('Server Errors:' + self.messages)


class Client(object):
    def __init__(self, address):
        assert isinstance(address, basestring), address
        self._socket = CONTEXT.socket(zmq.REQ)
        self._socket.connect(address)

    def _call(self, request):
        raw_request = request.SerializeToString()
        self._socket.send(raw_request, 0)
        raw_reply = self._socket.recv(0)
        reply = Response()
        reply.ParseFromString(raw_reply)
        for message in reply.error_log:
            WARN(message)
        if reply.error_log:
            raise ServerError(reply.error_log)
        return reply

    def ping(self):
        request = Request()
        self._call(request)

    def test_inference(self):
        request = Request()
        request.test_inference.SetInParent()
        reply = self._call(request)
        return reply.test_inference.fail_count

    def _simplify(self, codes):
        request = Request()
        request.simplify.SetInParent()
        for code in codes:
            request.simplify.codes.append(code)
        reply = self._call(request)
        return list(reply.simplify.codes)

    def simplify(self, codes):
        assert isinstance(codes, list), codes
        for code in codes:
            assert isinstance(code, basestring), code
        results = self._simplify(codes)
        assert len(results) == len(codes), results
        return results

    def _validate(self, codes):
        request = Request()
        request.validate.SetInParent()
        for code in codes:
            request.validate.codes.append(code)
        reply = self._call(request)
        results = []
        for result in reply.validate.results:
            results.append({
                'is_top': TROOL[result.is_top],
                'is_bot': TROOL[result.is_bot],
            })
        return results

    def validate(self, codes):
        assert isinstance(codes, list), codes
        for code in codes:
            assert isinstance(code, basestring), code
        results = self._validate(codes)
        assert len(results) == len(codes), results
        return results

    def _validate_corpus(self, lines):
        request = Request()
        request.validate_corpus.SetInParent()
        for line in lines:
            request_line = request.validate_corpus.lines.add()
            name = line['name']
            if name:
                request_line.name = name
            request_line.code = line['code']
        reply = self._call(request)
        results = []
        for result in reply.validate_corpus.results:
            results.append({
                'is_top': TROOL[result.is_top],
                'is_bot': TROOL[result.is_bot],
                'pending': result.pending,
            })
        return results

    def validate_corpus(self, lines):
        assert isinstance(lines, list), lines
        for line in lines:
            assert isinstance(line, dict), line
            assert 'name' in line
            assert 'code' in line
        results = self._validate_corpus(lines)
        assert len(results) == len(lines), results
        return results
