# some python methods to diagnose the data in the exceptions file

# get exception class name
def get_ex_class(ex):
    start = ex.find('class=')+len('class=')+1
    end = ex.find(';', start)
    return ex[start:end].replace('/', '.')


# get dict of exception class -> list of exceptions
def split_exs_by_class(exs):
	lst =  {}
	for ex in exs:
		cls=get_ex_class(ex)
		if not cls in lst:
			lst[cls]=[]
		lst[cls].append(ex)
	return lst


# print exceptions of each class to file
def print_exs_by_type(all_exs_by_type):
    for t in all_exs_by_type:
        open(f'all_exs_{t}.log', 'w').write('\n\n'.join(all_exs_by_type[t]))


# filter class load error not caused while still searching
def filter_load_errors(cls_load_exs):
    ret = []
    for ex in cls_load_exs:
        catch_line = ex.split('\n')[2]
        assert('will be caught in: ' in catch_line)
        if ('#loadClass :' not in catch_line and '#findClass :' not in catch_line):
            ret.append(ex)
    return ret


# exception file name
fname = 'cx_exceptions_???.log'

# list of all exceptions
all_exs=[e.strip() for e in open(fname).read().split('\n\n') if e.strip()]

# split exceptions by type
all_exs_by_type = split_exs_by_class(all_exs)

# get list of class load exceptions that aren't expected by the class loader
real_cls_not_found = filter_load_errors(all_exs_by_type['java.lang.ClassNotFoundException'])
