# Copyright (c) 2011 Red Hat, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
# implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import logging
from swift.common.constraints import check_utf8, check_metadata

from webob.exc import HTTPBadRequest, HTTPLengthRequired, \
    HTTPRequestEntityTooLarge


#: Max file size allowed for objects
MAX_FILE_SIZE = 0xffffffffffffffff
#: Max length of the name of a key for metadata
MAX_META_NAME_LENGTH = 128
#: Max length of the value of a key for metadata
MAX_META_VALUE_LENGTH = 256
#: Max number of metadata items
MAX_META_COUNT = 90
#: Max overall size of metadata
MAX_META_OVERALL_SIZE = 4096
#: Max object name length
MAX_OBJECT_NAME_LENGTH = 255
#: Max object list length of a get request for a container
CONTAINER_LISTING_LIMIT = 10000
#: Max container list length of a get request for an account
ACCOUNT_LISTING_LIMIT = 10000
MAX_ACCOUNT_NAME_LENGTH = 255
MAX_CONTAINER_NAME_LENGTH = 255

def validate_obj_name(obj):
    if len(obj) > MAX_OBJECT_NAME_LENGTH:
        logging.error('Object name too long %s' % obj)
        return False
    if obj == '.' or obj == '..':
        logging.error('Object name cannot be . or .. %s' % obj)
        return False

    return True

def check_object_creation(req, object_name):
    """
    Check to ensure that everything is alright about an object to be created.

    :param req: HTTP request object
    :param object_name: name of object to be created
    :raises HTTPRequestEntityTooLarge: the object is too large
    :raises HTTPLengthRequered: missing content-length header and not
                                a chunked request
    :raises HTTPBadRequest: missing or bad content-type header, or
                            bad metadata
    """
    if req.content_length and req.content_length > MAX_FILE_SIZE:
        return HTTPRequestEntityTooLarge(body='Your request is too large.',
                                         request=req,
                                         content_type='text/plain')
    if req.content_length is None and \
            req.headers.get('transfer-encoding') != 'chunked':
        return HTTPLengthRequired(request=req)
    if 'X-Copy-From' in req.headers and req.content_length:
        return HTTPBadRequest(body='Copy requests require a zero byte body',
                              request=req, content_type='text/plain')
    for obj in object_name.split('/'):
        if not validate_obj_name(obj):
            return HTTPBadRequest(body='Invalid object name %s' %
                    (obj), request=req,
                    content_type='text/plain')
    if 'Content-Type' not in req.headers:
        return HTTPBadRequest(request=req, content_type='text/plain',
                              body='No content type')
    if not check_utf8(req.headers['Content-Type']):
        return HTTPBadRequest(request=req, body='Invalid Content-Type',
                              content_type='text/plain')
    if 'x-object-manifest' in req.headers:
        value = req.headers['x-object-manifest']
        container = prefix = None
        try:
            container, prefix = value.split('/', 1)
        except ValueError:
            pass
        if not container or not prefix or '?' in value or '&' in value or \
                prefix[0] == '/':
            return HTTPBadRequest(
                request=req,
                body='X-Object-Manifest must in the format container/prefix')
    return check_metadata(req, 'object')

